/*
  Copyright 2009-2012 Lianqi Wang <lianqiw@gmail.com> <lianqiw@tmt.org>
  
  This file is part of Multithreaded Adaptive Optics Simulator (MAOS).

  MAOS is free software: you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version.

  MAOS is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  MAOS.  If not, see <http://www.gnu.org/licenses/>.
*/
extern "C"
{
#include <cuda.h>
#include "gpu.h"
}
#include "utils.h"
#include "wfs.h"
#include "recon.h"
#include "pcg.h"
#include "curmat.h"
#include "cucmat.h"
#include "accphi.h"
#undef TIMING
#define TIMING 0
#if !TIMING
#define TIC_test
#define tic_test
#define toc_test(A)
#else
#define TIC_test TIC
#define tic_test tic
#define toc_test(A) toc(A)
#endif
__global__ static void saloc2ptr_do(int (*restrict saptr)[2], float (*restrict saloc)[2], 
				    int nsa, float ox, float oy, float dx){
    const int step=blockDim.x * gridDim.x;
    const float dx1=1./dx;
    for(int isa=blockIdx.x * blockDim.x + threadIdx.x; isa<nsa; isa+=step){
	saptr[isa][0]=(int)roundf((saloc[isa][0]-ox)*dx1);
	saptr[isa][1]=(int)roundf((saloc[isa][1]-oy)*dx1);
    }
}
W01_T *gpu_get_W01(dsp *R_W0, dmat *R_W1){
    if(!R_W0 || !R_W1){
	error("R0, R1 must not be empty\n");
    }
    W01_T *W01=(W01_T*)calloc(1, sizeof(W01_T));
    cp2gpu(&W01->W1, R_W1);
    if(0){warning("change to 0\n");
	W01->nW0f=0;
	cp2gpu(&W01->W0p, R_W0);
    }else{
	/*W0 of partially illuminates subaps are stored as sparse matrix in
	  GPU. W0 of fully illuminated subaps are not.*/
	spint *pp=R_W0->p;
	spint *pi=R_W0->i;
	double *px=R_W0->x;
	dsp *W0new=spnew(R_W0->m, R_W0->n, R_W0->nzmax);
	spint *pp2=W0new->p;
	spint *pi2=W0new->i;
	double *px2=W0new->x;
	int *full;
	cudaMallocHost(&full, R_W0->n*sizeof(int));
	double W1max=dmax(R_W1);
	double thres=W1max*(1.f-1e-6);
	W01->W0v=(float)(W1max*4./9.);//max of W0 is 4/9 of max of W1. 
	info("W0v=%g\n", W01->W0v);
	int count=0;
	int count2=0;
	for(int ic=0; ic<R_W0->n; ic++){
	    pp2[ic]=count;
	    if(R_W1->p[ic]>thres){
		full[count2]=ic;
		count2++;
	    }else{
		int nv=pp[ic+1]-pp[ic];
		memcpy(pi2+count, pi+pp[ic], sizeof(spint)*nv);
		memcpy(px2+count, px+pp[ic], sizeof(double)*nv);
		count+=nv;
	    }
	}
	pp2[R_W0->n]=count;
	W0new->nzmax=count;
	dsp *W0new2=sptrans(W0new);
	cp2gpu(&W01->W0p, W0new2);
	cp2gpu(&W01->W0f, full, count2);
	W01->nW0f=count2;
	spfree(W0new);
	spfree(W0new2);
	cudaFreeHost(full);
    }
    return W01;
}
/*
  The caller must specify current GPU.
*/
static void gpu_setup_recon_do(const PARMS_T *parms, POWFS_T *powfs, RECON_T *recon){
    if(!cudata->recon){
	cudata->recon=new curecon_t;
    }
    curecon_t *curecon=cudata->recon;
    if(parms->recon.mvm && (!parms->gpu.tomo || !parms->gpu.fit)){
	return; /*Use CPU to assemble MVM*/
    }
    if(parms->recon.alg!=0){
	error("Only MVR is implemented in GPU\n");
    }
    cuwloc_t *cupowfs=cudata->powfs;
    
    curecon->wfsstream=new stream_t[parms->nwfsr];
    curecon->fitstream=new stream_t[parms->fit.nfit];
    curecon->psstream =new stream_t[recon->npsr];
    curecon->dmstream =new stream_t[recon->ndm];
    curecon->cgstream =new stream_t;
    curecon->wfsevent =(cudaEvent_t*)calloc(parms->nwfsr, sizeof(cudaEvent_t));
    for(int iwfs=0; iwfs<parms->nwfsr; iwfs++){
	DO(cudaEventCreateWithFlags(&curecon->wfsevent[iwfs], cudaEventDisableTiming));
    }
    if(parms->gpu.tomo){
	for(int ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
	    if(parms->powfs[ipowfs].skip) continue;
	    int nsa=powfs[ipowfs].pts->nsa;
	    cudaMalloc(&cupowfs[ipowfs].saptr, nsa*2*sizeof(int));
	    saloc2ptr_do<<<DIM(nsa,256)>>>
		(cupowfs[ipowfs].saptr, cupowfs[ipowfs].saloc, nsa, 
		 recon->pmap->ox, recon->pmap->oy, recon->pmap->dx);
	    if(recon->GP->p[ipowfs]){
		const int use_mat=parms->tomo.pos==2;
		if(use_mat){//normally true
		    dsp *GP=sptrans(recon->GP->p[ipowfs]);
		    spint *pp=GP->p;
		    spint *pi=GP->i;
		    double *px=GP->x;
		    dmat *partx=NULL;
		    dmat *party=NULL;
		    partx=dnew(9, nsa);
		    party=dnew(9, nsa);

		    int nsa=powfs[ipowfs].pts->nsa;
		    double dx1=1./recon->ploc->dx;
		
		    for(int ic=0; ic<GP->n; ic++){
			int isa=(ic<nsa)?ic:(ic-nsa);
			for(spint ir=pp[ic]; ir<pp[ic+1]; ir++){
			    int ix=pi[ir];
			    double lx=recon->ploc->locx[ix];
			    double ly=recon->ploc->locy[ix];
			    double sx=powfs[ipowfs].saloc->locx[isa];
			    double sy=powfs[ipowfs].saloc->locy[isa];
			    int zx=(int)round((lx-sx)*dx1);
			    int zy=(int)round((ly-sy)*dx1);
			    /**
			       Squeeze the weights to closed points in 3x3 in the subaperture.
			       Does not work well. simply drop these points doesn't work either.
			    */
			    if(zx<0 || zx>2 || zy<0 || zy>2){
				warning("isa=%d, zxy=%d %d\n", isa, zx, zy);
			    }
			    if(zx<0) zx=0;
			    if(zx>2) zx=2;
			    if(zy<0) zy=0;
			    if(zy>2) zy=2;
			    if(ic<nsa){/*x */
				partx->p[9*isa+zx+zy*3]+=px[ir];
			    }else{/*y */
				party->p[9*isa+zx+zy*3]+=px[ir];
			    }
			}
		    }
		    cp2gpu(&cupowfs[ipowfs].GPpx, partx);
		    cp2gpu(&cupowfs[ipowfs].GPpy, party);
		    dfree(partx);
		    dfree(party);
		    spfree(GP);
		}else{/*use sparse */
		    cp2gpu(&cupowfs[ipowfs].GP, recon->GP->p[ipowfs]);
		}
	    }else{
		error("GP is required\n");
	    }
	}
 
	curecon->l2c=(float*)calloc(recon->npsr, sizeof(float));
	for(int ips=0; ips<recon->npsr; ips++){
	    float tmp=laplacian_coef(recon->r0, recon->wt->p[ips], recon->xmap[ips]->dx)*0.25f;
	    curecon->l2c[ips]=tmp*tmp*TOMOSCALE;
	}
	if(parms->tomo.piston_cr){
	    curecon->zzi=(int*)calloc(recon->npsr, sizeof(int));
	    curecon->zzv=(float*)calloc(recon->npsr, sizeof(float));
	    for(int ips=0; ips<recon->npsr; ips++){
		double r0=recon->r0;
		double dx=recon->xloc[ips]->dx;
		double wt=recon->wt->p[ips];
		int icenter=loccenter(recon->xloc[ips]);
		curecon->zzi[ips]=icenter;
		curecon->zzv[ips]=pow(laplacian_coef(r0,wt,dx),2)*TOMOSCALE*1e-6;
	    }
	}
	curecon->neai=curcellnew(parms->nwfsr, 1);
	/*convert recon->saneai to our format. */
	for(int iwfs=0; iwfs<parms->nwfsr; iwfs++){
	    int ipowfs=parms->wfsr[iwfs].powfs;
	    int nsa=powfs[ipowfs].pts->nsa;
	    int iwfs0=parms->powfs[ipowfs].wfs[0];/*first wfs in this group. */
	    if(iwfs!=iwfs0 && recon->saneai->p[iwfs+iwfs*parms->nwfsr]->p
	       ==recon->saneai->p[iwfs0+iwfs0*parms->nwfsr]->p){
		curecon->neai->p[iwfs]=curref(curecon->neai->p[iwfs0]);
	    }else{
		dsp *nea=recon->saneai->p[iwfs+iwfs*parms->nwfsr];
		spint *pp=nea->p;
		spint *pi=nea->i;
		double *px=nea->x;
		
		float (*neai)[3]=(float(*)[3])calloc(3*nsa, sizeof(float));
		if(nea->n!=2*nsa) error("nea doesn't have 2nsa x 2nsa dimension\n");
		for(int ic=0; ic<nea->n; ic++){
		    for(spint ir=pp[ic]; ir<pp[ic+1]; ir++){
			int ix=pi[ir];
			int isa=ic<nsa?ic:ic-nsa;
			float val=(float)px[ir]*TOMOSCALE;
			if(ix==ic){/*diagonal part. */
			    if(ic==isa){/*x */
				neai[isa][0]=val;
			    }else{/*y */
				neai[isa][1]=val;
			    }
			}else if(ix==ic-nsa || ix==ic+nsa){/*cross part. symmetric. */
			    neai[isa][2]=val;
			}else{
			    error("saneai has invalid format\n");
			}
		    }
		}
		curecon->neai->p[iwfs]=curnew(3, nsa);
		DO(cudaMemcpy(curecon->neai->p[iwfs]->p, neai, 3*nsa*sizeof(float), cudaMemcpyHostToDevice));
		free(neai);
	    }
	}/*for iwfs */
	CUDA_SYNC_DEVICE;
	if(recon->PTT && !curecon->PTT){
	    cp2gpu(&curecon->PTT, recon->PTT);
	}
	if(recon->PDF && !curecon->PDF){
	    cp2gpu(&curecon->PDF, recon->PDF);
	}
	if(parms->tomo.precond==1){/*fdpcg*/
	    FDPCG_T *fdpcg=recon->fdpcg;
	    int nb=fdpcg->Mbinv->nx;
	    int bs=fdpcg->Mbinv->p[0]->nx;
	    cp2gpu(&curecon->fd_perm, fdpcg->perm, nb*bs);//not bs*bs
	    //curecon->fd_Mb=cuccellnew(nb, 1, bs, bs);
	    cp2gpu(&curecon->fd_Mb, fdpcg->Mbinv);
	    curecon->fd_nxtot=nb*bs;
	    int nps=recon->npsr;
	    int count=0;
	    int osi=-1;
	    int start[nps];
	    for(int ips=0; ips<nps; ips++){
		if(osi != parms->atmr.os[ips]){
		    start[count]=ips;
		    osi = parms->atmr.os[ips];
		    count++;
		}
	    }
	    curecon->fd_fft=(cufftHandle*)calloc(count, sizeof(cufftHandle));
	    curecon->fd_fftnc=count;
	    curecon->fd_fftips=(int*)calloc(count+1, sizeof(int));
	    for(int ic=0; ic<count; ic++){
		curecon->fd_fftips[ic]=start[ic];
	    }
	    curecon->fd_fftips[count]=nps;
	    for(int ic=0; ic<count; ic++){
		int ncomp[2];
		ncomp[0]=recon->xnx[start[ic]];
		ncomp[1]=recon->xny[start[ic]];
		cufftPlanMany(&curecon->fd_fft[ic], 2, ncomp, NULL, 1, 0, NULL, 1, 0, 
			      CUFFT_C2C, curecon->fd_fftips[ic+1]-curecon->fd_fftips[ic]);
		cufftSetStream(curecon->fd_fft[ic], curecon->cgstream[0]);
	    }
	    /* notice: performance may be improved by using
	       R2C FFTs instead of C2C. Need to update perm
	       and Mbinv to use R2C.*/
	}
	if(parms->tomo.alg==0){//CBS
	    chol_convert(recon->RL.C, 0);
	    cp2gpu(&curecon->RCl, recon->RL.C->Cl);
	    cp2gpu(&curecon->RCp, recon->RL.C->Cp, recon->RL.C->Cl->m);
	    if(recon->RL.Up){
		cp2gpu(&curecon->RUp, recon->RL.Up);
		cp2gpu(&curecon->RVp, recon->RL.Vp);
	    }
	}else if(parms->tomo.alg==2){//SVD
	    cp2gpu(&curecon->RMI, recon->RL.MI);
	}
	curecon->opdr=curcellnew(recon->npsr, 1, recon->xnx, recon->xny);
	curecon->opdr_vec=curcellnew(recon->npsr, 1);
	for(int ips=0; ips<recon->npsr; ips++){
	    curecon->opdr_vec->p[ips]=curref(curecon->opdr->p[ips]);
	    curecon->opdr_vec->p[ips]->nx=curecon->opdr->p[ips]->nx*curecon->opdr->p[ips]->ny;
	    curecon->opdr_vec->p[ips]->ny=1;
	}
	const int nwfs=parms->nwfsr;
	int nxp=recon->pmap->nx;
	int nyp=recon->pmap->ny;
  
	curecon->opdwfs=curcellnew(nwfs, 1);
	curecon->grad=curcellnew(nwfs, 1);/*intermediate. */
	for(int iwfs=0; iwfs<nwfs; iwfs++){
	    const int ipowfs = parms->wfsr[iwfs].powfs;
	    if(parms->powfs[ipowfs].skip) continue;
	    curecon->opdwfs->p[iwfs]=curnew(nxp, nyp);
	    const int nsa=powfs[ipowfs].pts->nsa;
	    curecon->grad->p[iwfs]=curnew(nsa*2,1);
	}
	if(parms->tomo.precond==1){
	    curecon->fd_xhat1=cuccellnew(recon->npsr, 1, recon->xnx, recon->xny);
	    curecon->fd_xhat2=cuccellnew(recon->npsr, 1, recon->xnx, recon->xny);
	}
    }
    if(parms->gpu.fit){
	if(parms->gpu.fit==1){ /*For fitting using sparse matrix*/
	    cp2gpu(&curecon->FR, &recon->FR);
	    cp2gpu(&curecon->FL, &recon->FL);
	    curecon->FR.fitstream=curecon->fitstream;
	    curecon->FR.dmstream=curecon->dmstream;
	    curecon->FL.fitstream=curecon->fitstream;
	    curecon->FL.dmstream=curecon->dmstream;
	    curecon->dmfit=curcellnew(parms->ndm, 1, recon->anloc, (long*)NULL);
	    curecon->dmfit_vec=curecon->dmfit;
	}else if(parms->gpu.fit==2){ /*For fitting using ray tracing*/
	    if(!recon->W0 || !recon->W1){
		error("W0, W1 is required\n");
	    }
	    if(parms->sim.idealfit){
		cp2gpu(&curecon->floc, recon->floc);
		curecon->nfloc=recon->floc->nloc;
	    }
	    curecon->W01=gpu_get_W01(recon->W0, recon->W1);
	    cp2gpu(&curecon->fitNW, recon->fitNW);
	    cp2gpu(&curecon->actslave, recon->actslave);
	    curecon->dmfit=curcellnew(parms->ndm, 1);
	    curecon->dmfit_vec=curcellnew(parms->ndm, 1);
	    int ntotact=0;
	    for(int idm=0; idm<parms->ndm; idm++){
		ntotact+=recon->amap[idm]->nx*recon->amap[idm]->ny;
	    }
	    curecon->dmfit->m=curnew(ntotact, 1);
	    int ct=0;
	    for(int idm=0; idm<parms->ndm; idm++){
		curecon->dmfit->p[idm]=new cumap_t(recon->amap[idm]->nx,recon->amap[idm]->ny,
						   curecon->dmfit->m->p+ct, 0);
		ct+=recon->amap[idm]->nx*recon->amap[idm]->ny;
		curecon->dmfit_vec->p[idm]=curref(curecon->dmfit->p[idm]);
		curecon->dmfit_vec->p[idm]->nx=curecon->dmfit_vec->p[idm]->nx
		    *curecon->dmfit_vec->p[idm]->ny;
		curecon->dmfit_vec->p[idm]->ny=1;
	    }
	}
	curecon->cubic_cc=new float *[parms->ndm];
	for(int idm=0; idm<parms->ndm; idm++){
	    curecon->cubic_cc[idm]=gpu_dmcubic_cc(parms->dm[idm].iac);
	}
	if(parms->fit.alg==0){
	    chol_convert(recon->FL.C, 0);
	    cp2gpu(&curecon->FCl, recon->FL.C->Cl);
	    cp2gpu(&curecon->FCp, recon->FL.C->Cp, recon->FL.C->Cl->m);
	    if(recon->FL.Up){
		cp2gpu(&curecon->FUp, recon->FL.Up);
		cp2gpu(&curecon->FVp, recon->FL.Vp);
	    }
	}else if(parms->fit.alg==2){
	    cp2gpu(&curecon->FMI, recon->FL.MI);
	}
	const int nfit=parms->fit.nfit;
	curecon->opdfit=curcellnew(nfit, 1);
	curecon->opdfit2=curcellnew(nfit, 1);
	for(int ifit=0; ifit<nfit; ifit++){
	    curecon->opdfit->p[ifit] =curnew(recon->fmap->nx, recon->fmap->ny);
	    curecon->opdfit2->p[ifit]=curnew(recon->fmap->nx, recon->fmap->ny);
	}
	curecon->pis=curnew(parms->fit.nfit, 1);
    }
    if(recon->RFlgsx){
	cp2gpu(&curecon->RFlgsx, recon->RFlgsx);
    }
    if(recon->RFngsx){
	cp2gpu(&curecon->RFngsx, recon->RFngsx);
    }
    gpu_print_mem("recon init");
}
void gpu_setup_recon(const PARMS_T *parms, POWFS_T *powfs, RECON_T *recon){
    if(parms->recon.mvm && parms->gpu.tomo && parms->gpu.fit && !parms->load.MVM){
	for(int igpu=0; igpu<NGPU; igpu++){
	    gpu_set(igpu);
	    gpu_setup_recon_do(parms, powfs, recon);
	}
    }else{
	gpu_set(gpu_recon);
	gpu_setup_recon_do(parms, powfs, recon);
    }
}
static void gpu_recon_free_do(){
    curecon_t *curecon=cudata->recon;
    if(!curecon) return;
    curcellfree(curecon->neai);
    curcellfree(curecon->opdwfs);
    curcellfree(curecon->grad); 
    curcellfree(curecon->opdr); 
    curcellfree(curecon->opdr_vec); 
    cudaFree(curecon->fd_perm);
    cuccellfree(curecon->fd_Mb);
    free(curecon->fd_fftips);
    cuccellfree(curecon->fd_xhat1);
    cuccellfree(curecon->fd_xhat2);
    if(curecon->dmfit_vec!=curecon->dmfit){
	curcellfree(curecon->dmfit_vec);
    }else{
	curecon->dmfit_vec=NULL;
    }
    curcellfree(curecon->dmfit);
    free(curecon->l2c);
    free(curecon->zzi);
    free(curecon->zzv);
    if(curecon->W01){
	W01_T *W01=curecon->W01;
	curfree(W01->W1);
	delete W01->W0p;
	cudaFree(W01->W0f);
    }
    curcellfree(curecon->opdfit);
    curcellfree(curecon->opdfit2);
    curfree(curecon->pis);
    cudaFree(curecon->floc);
    curcellfree(curecon->fitNW);
    delete curecon->actslave;
    delete curecon->RCl;
    cudaFree(curecon->RCp);
    curfree(curecon->RUp);
    curfree(curecon->RVp);
    curfree(curecon->RMI);
    delete curecon->FCl;
    cudaFree(curecon->FCp);
    curfree(curecon->FUp);
    curfree(curecon->FVp);
    curfree(curecon->FMI);
    //    delete curecon;
}
void gpu_recon_free(){
    gpu_set(gpu_recon);
    gpu_recon_free_do();
}
static __global__ void 
copy_mvm_do(float *dest, long stride, float *in, long n){
    const int step=blockDim.x * gridDim.x;
    for(int i=blockIdx.x * blockDim.x + threadIdx.x; i<n; i+=step){
	dest[i*stride]=in[i];
    }
}
typedef struct MVM_IGPU_T{
    const PARMS_T *parms;
    RECON_T *recon;
    POWFS_T *powfs;
    float *FLI;
    smat *residual;
    long (*curp)[2];
    pthread_mutex_t *mutex;
    int ntotact;
    int ntotgrad;
}MVM_IGPU_T;
void gpu_setup_recon_mvm_igpu(thread_t *info){
    TIC;tic;
    MVM_IGPU_T *data=(MVM_IGPU_T*)info->data;
    const PARMS_T *parms=data->parms;
    RECON_T *recon=data->recon;
    POWFS_T *powfs=data->powfs;
    const float *FLI=data->FLI;
    smat *residual=data->residual;
    long (*curp)[2]=data->curp;
    const int ntotact=data->ntotact;
    const int ntotgrad=data->ntotgrad;
    int igpu=info->ithread;
    gpu_set(igpu);
    curecon_t *curecon=cudata->recon;
    /*Tomography*/
    G_PREFUN prefun=NULL;
    void *predata=NULL;
    if(parms->tomo.precond==1){
	prefun=gpu_Tomo_fdprecond;
	predata=(void*)recon;
    }
    G_CGFUN cg_fun;
    void *cg_data;
    curcell *eyec=NULL;/* Only use eyec for CG.*/
    float eye2[2]={0,1.};
    float eye1[1]={1.};
    const int nwfs=parms->nwfs;
    const int ndm=parms->ndm;
    if(parms->gpu.fit==1){//sparse matrix
	cg_fun=(G_CGFUN) cumuv;
	cg_data=&curecon->FL;
    }else{
	cg_fun=(G_CGFUN) gpu_FitL;
	cg_data=recon;
    }
    if(!FLI){
	if(parms->fit.square){
	    eyec=curcellnew(ndm, 1, recon->anx, recon->any);
	}else{
	    eyec=curcellnew(ndm, 1, recon->anloc, (long*)0);
	}
    }
    curecon->MVM=curcellnew(ndm, nwfs);
    for(int idm=0; idm<ndm; idm++){
	for(int iwfs=0; iwfs<nwfs; iwfs++){
	    int ipowfs=parms->wfs[iwfs].powfs;
	    if(!parms->powfs[ipowfs].skip){
		curecon->MVM->p[idm+iwfs*ndm]
		    =curnew(recon->anloc[idm], powfs[ipowfs].saloc->nloc*2);
	    }
	}
    }
    curcell *dmfit=curcellnew(curecon->dmfit);
    curcell *opdx=curcellnew(recon->npsr, 1, recon->xnx, recon->xny);
    curcell *opdr=curcellnew(recon->npsr, 1, recon->xnx, recon->xny);
    curcell *grad=curcellnew(parms->nwfs, 1, recon->ngrad, (long*)0);
    if(ntotact==0){
	error("ntotact=0;\n");
    }
    for(int iact=info->start; iact<info->end; iact++){
	int curdm=curp[iact][0];
	int curact=curp[iact][1];
	TIC_test;tic_test;
	if(info->ithread==0){
	    if(!detached){
		info2("%6d of %6d\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b", iact*NGPU, ntotact);
	    }else if(iact % 100==0){
		info2("%6d of %6d\n", iact*NGPU, ntotact);
	    }
	}
	if(eyec){
	    if(iact){
		cudaMemcpyAsync(eyec->m->p+iact-1, eye2, 2*sizeof(float),
				cudaMemcpyHostToDevice, curecon->cgstream[0]);
	    }else{
		cudaMemcpyAsync(eyec->m->p+iact, eye1, sizeof(float), 
				cudaMemcpyHostToDevice, curecon->cgstream[0]);
	    }
	}
	toc_test("copy");
	if(!recon->actcpl || recon->actcpl->p[curdm]->p[curact]>EPS){
	    if(eyec){
		/*Fitting operator*/
		curcellzero(dmfit, curecon->cgstream[0]);//temp
		if(gpu_pcg(&dmfit, (G_CGFUN)cg_fun, cg_data, NULL, NULL, eyec,
			   parms->recon.warm_restart, parms->fit.maxit, curecon->cgstream[0])>1.){
		    error("Fit CG failed\n");
		}
	    }else{
		cudaMemcpyAsync(dmfit->m->p, FLI+iact*ntotact, sizeof(float)*ntotact, 
				cudaMemcpyHostToDevice, curecon->cgstream[0]);
	    }
	    cudaStreamSynchronize(curecon->cgstream[0]);
	    toc_test("FitL");
	    /*Transpose of fitting operator*/
	    if(parms->gpu.fit==1){//sparse matrix
		cumuv_trans(&opdx, 0, &curecon->FR, dmfit, 1);
	    }else{
		gpu_FitRt(&opdx, 0, recon, dmfit, 1);
	    }
	    toc_test("FitRt");
	    switch(parms->tomo.alg){
	    case 0:
		if(!opdr->m || !opdx->m){
		    error("opdr and opdx must be continuous\n");
		}
		cuchol_solve(opdr->m->p, curecon->RCl, curecon->RCp, opdx->m->p, curecon->cgstream[0]);
		if(curecon->RUp){
		    curmat *tmp=curnew(curecon->RVp->ny, 1);
		    curmv(tmp->p, 0, curecon->RVp, opdx->m->p, 't', -1, curecon->cgstream[0]);
		    curmv(opdr->m->p, 1, curecon->RUp, tmp->p, 'n', 1, curecon->cgstream[0]);
		    cudaStreamSynchronize(curecon->cgstream[0]);
		    curfree(tmp);
		}
		break;
	    case 1:{
		curcellzero(opdr, curecon->cgstream[0]);
		int disablelrt=curecon->disablelrt;
		curecon->disablelrt=1;
		/*disable the t/t removal lrt in split tomo that creats problem in fdpcg mode*/
		if((residual->p[iact]=gpu_pcg(&opdr, gpu_TomoL, recon, prefun, predata, opdx, 
					      parms->recon.warm_restart, parms->tomo.maxit,
					      curecon->cgstream[0]))>1){
		    error("Tomo CG failed\n");
		}
		curecon->disablelrt=disablelrt;
	    }
		break;
	    case 2:
		curmv(opdr->m->p, 0, curecon->RMI, opdx->m->p, 'n', 1, curecon->cgstream[0]);
		break;
	    default:
		error("Invalid");
	    }
	    toc_test("TomoL");
	    /*Right hand size*/
	    gpu_TomoRt(&grad, 0, recon, opdr, 1);
	    toc_test("TomoR");
	    //copy grad out and transpose
	    for(int iwfs=0; iwfs<parms->nwfs; iwfs++){
		curmat *to=curecon->MVM->p[curdm+ndm*iwfs];
		if(to){
		    copy_mvm_do<<<DIM(ntotgrad, 256), 0, curecon->cgstream[0]>>>
			(to->p+curact, recon->anloc[curdm], grad->p[iwfs]->p, recon->ngrad[iwfs]);
		}
	    }
	    /*{
	      curcellwrite(eyec, "gpu_eyec_%d", iact);
	      curcellwrite(dmfit,"gpu_dmfit_%d", iact);
	      curcellwrite(opdx, "gpu_opdx_%d", iact);
	      curcellwrite(opdr, "gpu_opdr_%d", iact);
	      curcellwrite(grad, "gpu_grad_%d", iact);
	      }*/
	}
	toc_test("Copy");
    }//for iact
    //warning("\n\nFree the various data for GPU tomo/fit\n\n");
    curcellfree(dmfit);
    curcellfree(opdx);
    curcellfree(opdr);
    curcellfree(grad);
    curcellfree(eyec);
    if(parms->save.setup){
	swrite(residual, "%s/MVM_RLpcgr_%d", dirsetup, igpu);
    }
    
    if(parms->save.setup){
	curcellwrite(curecon->MVM, "%s/MVM_%d.bin", dirsetup, igpu);
    }
    toc2("MVM on GPU %d", igpu);tic;
    /*Add all to CPU*/
    if(NGPU>1 || parms->sim.mvmport){
	cp2cpu(&recon->MVM, 1, curecon->MVM, 1, curecon->cgstream[0], data->mutex);
	curfree(curecon->MVM);
    }
    toc2("cp2cpu on GPU %d", igpu);
}
void gpu_setup_recon_mvm(const PARMS_T *parms, RECON_T *recon, POWFS_T *powfs){
    TIC;tic;
    if(parms->recon.alg!=0){
	error("Please adept to LSR\n");
    } 
    if(parms->load.MVM || !parms->gpu.tomo || !parms->gpu.fit){
	gpu_set(gpu_recon);
	curecon_t *curecon=cudata->recon;
	if(parms->load.MVM){
	    recon->MVM=dcellread("%s", parms->load.MVM);
	}else{
	    setup_recon_mvr_mvm(recon, parms, powfs);
	}
	if(!parms->sim.mvmport){
	    cp2gpu(&curecon->MVM, recon->MVM);
	    dcellfree(recon->MVM);
	}
    }else{
	info2("Assembling MVR MVM in GPU\n");
	int ntotact=0;
	int ntotgrad=0;
	const int ndm=parms->ndm;
	for(int idm=0; idm<ndm; idm++){
	    ntotact+=recon->anloc[idm];
	} 
	long (*curp)[2]=(long(*)[2])malloc(ntotact*2*sizeof(long));
	int nact=0;
	for(int idm=0; idm<ndm; idm++){
	    for(int iact=0; iact<recon->anloc[idm]; iact++){
		curp[nact+iact][0]=idm;
		curp[nact+iact][1]=iact;
	    }
	    nact+=recon->anloc[idm];
	}   
	for(int iwfs=0; iwfs<parms->nwfs; iwfs++){
	    ntotgrad+=recon->ngrad[iwfs];
	}
	dmat *FLId=NULL;    /* MI is inv(FL) for direct methods*/
	switch(parms->fit.alg){
	case 0:{
	    dmat *eye=dnew(ntotact, ntotact);
	    daddI(eye, 1);
	    FLId=dnew(ntotact, ntotact);
	    muv_direct_solve(&FLId, &recon->FL, eye);
	    dfree(eye);
	    toc("Fit CBS");tic;
	}
	    break;
	case 1:
	    break;
	case 2:
	    FLId=dref(recon->FL.MI);
	    break;
	default:
	    error("Invalid fit.alg=%d\n", parms->fit.alg);
	}
	smat *residual=NULL;
	float *FLI=NULL;
	if(FLId){
	    dwrite(FLId, "FLI");
	    FLI=(float*)malloc4async(sizeof(float)*ntotact*ntotact);
	    for(long i=0; i<ntotact*ntotact; i++){
		FLI[i]=(float)FLId->p[i];
	    }
	    dfree(FLId);
	}
	if(parms->tomo.alg==1){
	    residual=snew(ntotact, 1);
	}
	PNEW(mutex);
	MVM_IGPU_T data={parms, recon, powfs, FLI, residual, curp, &mutex, ntotact, ntotgrad};
	int nthread=NGPU;
	thread_t info[NGPU];
	thread_prep(info, 0, ntotact, nthread, gpu_setup_recon_mvm_igpu, &data);
	CALL_THREAD(info, nthread, 1);
	if(recon->MVM && parms->save.setup){
	    dcellwrite(recon->MVM, "%s/MVM.bin", dirsetup);
	}
	if(!parms->sim.mvmport && NGPU>1){
	    gpu_set(gpu_recon);
	    cp2gpu(&cudata->recon->MVM, recon->MVM);
	    dcellfree(recon->MVM);
	}
	free(curp);
	if(FLI) free4async(FLI);
    }//if assemble in gpu
    for(int igpu=0; igpu<NGPU; igpu++){
	gpu_set(igpu);
	gpu_recon_free_do();
	CUDA_SYNC_DEVICE;
    }///for GPU
    toc("MVM Final");
    gpu_print_mem("MVM");
}
/*update reconstruction parameters after slodar.*/
void gpu_update_recon(const PARMS_T *parms, RECON_T *recon){
    gpu_set(gpu_recon);
    curecon_t *curecon=cudata->recon;
    for(int ips=0; ips<recon->npsr; ips++){
	float tmp=laplacian_coef(recon->r0, recon->wt->p[ips], recon->xmap[ips]->dx)*0.25f;
	curecon->l2c[ips]=tmp*tmp*TOMOSCALE;
    }
    if(parms->tomo.piston_cr){
	for(int ips=0; ips<recon->npsr; ips++){
	    double r0=recon->r0;
	    double dx=recon->xloc[ips]->dx;
	    double wt=recon->wt->p[ips];
	    int icenter=loccenter(recon->xloc[ips]);
	    curecon->zzi[ips]=icenter;
	    curecon->zzv[ips]=pow(laplacian_coef(r0,wt,dx),2)*TOMOSCALE*1e-6;
	}
    }
}
void gpu_recon_reset(const PARMS_T *parms){/*reset warm restart.*/
    gpu_set(gpu_recon);
    curecon_t *curecon=cudata->recon;
    curcellzero(curecon->opdr, 0);
    curcellzero(curecon->dmfit, 0);
    if(curecon->dm_wfs){
	for(int iwfs=0; iwfs<parms->nwfs; iwfs++){
	    curcellzero(curecon->dm_wfs[iwfs], 0);
	}
    }
    if(curecon->dm_evl){
	for(int ievl=0; ievl<parms->evl.nevl; ievl++){
	    curcellzero(curecon->dm_evl[ievl], 0);
	}
    }
    for(int igpu=0; igpu<NGPU; igpu++){
	gpu_set(igpu);
	curcellzero(cudata->dm_wfs,0);
	curcellzero(cudata->dm_evl,0);
    }
    cudaStreamSynchronize(0);
}

void gpu_tomo(SIM_T *simu){
    gpu_set(gpu_recon);
    curecon_t *curecon=cudata->recon;
    TIC_test;tic_test;
    const PARMS_T *parms=simu->parms;
    RECON_T *recon=simu->recon;
    if(parms->tomo.pos!=2){
	TO_IMPLEMENT;
    }
    if(curecon->PDF){
	TO_IMPLEMENT;
    }
    /*first send gradients to GPU. can be skipped if keep grad in gpu. fast though. */
    /*Create temporary memory */
    curecon->reconisim=simu->reconisim;
#if 0
    gpu_tomo_test(simu);
#endif
    toc_test("Before gradin");
    cp2gpu(&curecon->gradin, parms->tomo.psol?simu->gradlastol:simu->gradlastcl);
    toc_test("Gradin");
    curcell *rhs=NULL;
    gpu_TomoR(&rhs, 0, recon, curecon->gradin, 1);
    toc_test("TomoR");
    switch(parms->tomo.alg){
    case 0:
	if(!curecon->opdr->m){
	    error("opdr must be continuous\n");
	}
	if(!rhs->m){
	    error("rhs must be continuous\n");
	}
	cuchol_solve(curecon->opdr->m->p, curecon->RCl, curecon->RCp, rhs->m->p, curecon->cgstream[0]);
	if(curecon->RUp){
	    curmat *tmp=curnew(curecon->RVp->ny, 1);
	    curmv(tmp->p, 0, curecon->RVp, rhs->m->p, 't', -1, curecon->cgstream[0]);
	    curmv(curecon->opdr->m->p, 1, curecon->RUp, tmp->p, 'n', 1, curecon->cgstream[0]);
	    cudaStreamSynchronize(curecon->cgstream[0]);
	    curfree(tmp);
	}
	/*{
	  curcellwrite(rhs, "GPU_RHS");
	  curcellwrite(curecon->opdr, "GPU_OPDR");
	  muv_solve(&simu->opdr, &recon->RL, &recon->RR, simu->gradlastol);
	  dcellwrite(simu->opdr, "CPU_OPDR");
	  exit(1);
	  }*/
	break;
    case 1:{
	G_PREFUN prefun=NULL;
	void *predata=NULL;
	if(parms->tomo.precond==1){
	    prefun=gpu_Tomo_fdprecond;
	    predata=(void*)recon;
	}
	if(gpu_pcg(&curecon->opdr, gpu_TomoL, recon, prefun, predata, rhs, 
		   simu->parms->recon.warm_restart, parms->tomo.maxit, curecon->cgstream[0])>1){
	    error("Tomo CG failed\n");
	}
	toc_test("TomoL CG");
    }break;
    case 2:
	curmv(curecon->opdr->m->p, 0, curecon->RMI, rhs->m->p, 'n', 1, curecon->cgstream[0]);
	break;
    default:
	error("Invalid");
    }
    curcellfree(rhs); rhs=NULL;
    if(!parms->gpu.fit || parms->save.opdr || parms->recon.split==2 || (recon->moao && !parms->gpu.moao)){
	cp2cpu(&simu->opdr, 0, curecon->opdr_vec, 1, curecon->cgstream[0]);
    }
    if(curecon->RFlgsx){
	curcell *focus=NULL;
	curcellmm(&focus, 0, curecon->RFlgsx, curecon->opdr_vec, "nn", 1, curecon->cgstream[0]);
	cp2cpu(&simu->focuslgsx, 0, focus, 1, curecon->cgstream[0]);
    }
    if(curecon->RFngsx){
	curcell *focus=NULL;
	curcellmm(&focus, 0, curecon->RFngsx, curecon->opdr_vec, "nn", 1, curecon->cgstream[0]);
	cp2cpu(&simu->focusngsx, 0, focus, 1, curecon->cgstream[0]);
    }
    cudaStreamSynchronize(curecon->cgstream[0]);
    toc_test("Tomo");
}

void gpu_fit(SIM_T *simu){
    TIC_test;tic_test;
    gpu_set(gpu_recon);
    curecon_t *curecon=cudata->recon;
    const PARMS_T *parms=simu->parms;
    const RECON_T *recon=simu->recon;
    if(!parms->gpu.tomo){
	cp2gpu(&curecon->opdr_vec, simu->opdr);
    }
#if 0
    gpu_fit_test(simu);
#endif
    toc_test("Before FitR");
    curcell *rhs=NULL;
    G_CGFUN cg_fun;
    void *cg_data;
    if(parms->gpu.fit==1){//sparse matrix
	cumuv(&rhs, 0, &curecon->FR, curecon->opdr, 1);
	cg_fun=(G_CGFUN) cumuv;
	cg_data=&curecon->FL;
    }else{
	gpu_FitR(&rhs, 0, recon, curecon->opdr, 1);
	cg_fun=(G_CGFUN) gpu_FitL;
	cg_data=(void*)recon;
    }
    toc_test("FitR");
    switch(parms->fit.alg){
    case 0:
	cuchol_solve(curecon->dmfit->m->p, curecon->FCl, curecon->FCp, rhs->m->p, curecon->cgstream[0]);
	if(curecon->FUp){
	    curmat *tmp=curnew(curecon->FVp->ny, 1);
	    curmv(tmp->p, 0, curecon->FVp, rhs->m->p, 't', -1, curecon->cgstream[0]);
	    curmv(curecon->dmfit->m->p, 1, curecon->FUp, tmp->p, 'n', 1, curecon->cgstream[0]);
	    cudaStreamSynchronize(curecon->cgstream[0]);
	    curfree(tmp);
	}
	break;
    case 1:
	if(gpu_pcg(&curecon->dmfit, (G_CGFUN)cg_fun, cg_data, NULL, NULL, rhs,
		   simu->parms->recon.warm_restart, parms->fit.maxit, curecon->cgstream[0])>1){
	    error("DM Fitting PCG failed\n");
	}
	break;
    case 2:
	curmv(curecon->dmfit->m->p, 0, curecon->FMI, rhs->m->p, 'n', 1, curecon->cgstream[0]);
	break;
    default:
	error("Invalid");
    }
    cp2cpu(&simu->dmfit, 0, curecon->dmfit_vec, 1, curecon->cgstream[0]);
    toc_test("FitL CG");
    cudaStreamSynchronize(curecon->cgstream[0]);
    /*Don't free opdr. */
    curcellfree(rhs); rhs=NULL;
    toc_test("Fit");
}
void gpu_recon_mvm(SIM_T *simu){
    const PARMS_T *parms=simu->parms;
    gpu_set(gpu_recon);
    curecon_t *curecon=cudata->recon;
    cp2gpu(&curecon->gradin, parms->tomo.psol?simu->gradlastol:simu->gradlastcl);
    curcellmm(&curecon->dmfit_vec, 0., curecon->MVM, curecon->gradin,"nn", 1., curecon->cgstream[0]);
    cp2cpu(&simu->dmerr, 0., curecon->dmfit_vec, 1., curecon->cgstream[0]);
}
