/*
  Copyright 2009-2016 Lianqi Wang <lianqiw-at-tmt-dot-org>
  
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
#include "utils.h"
#include "accphi.h"
#include <curand_kernel.h>
#include "wfs.h"
#include "cudata.h"
#include "cucmat.h"
#ifdef __cplusplus
extern "C"{
#endif
#include "../maos/pywfs.h"
#include "../maos/sim.h"
#ifdef __cplusplus
}
#endif

__global__ static void
pywfs_grad_do(Real *grad, const Real *ints, const Real *saa, const Real *isum, const Real *goff, Real gain, int nsa){
    const Real alpha0=gain*nsa/(*isum);
    for(int i=threadIdx.x + blockIdx.x * blockDim.x; i<nsa; i+=blockDim.x * gridDim.x){
	const Real alpha=alpha0/saa[i];
	grad[i]=(-ints[i]+ints[i+nsa]-ints[nsa*2+i]+ints[nsa*3+i])*alpha-goff[i];
	grad[i+nsa]=(-ints[i]-ints[i+nsa]+ints[nsa*2+i]+ints[nsa*3+i])*alpha-goff[i+nsa];
    }
}
void pywfs_grad(curmat &grad, /**<[out] gradients*/
		const curmat &ints, /**<[in] Intensity*/
		const curmat &saa,  /**<[in] Subaperture normalized area*/
		curmat &isum, /**<[out] Sum intensity*/
		const curmat &goff, /**<[in] Gradient of flat wavefront*/
		Real gain,   /**<[in] Gain*/
		cudaStream_t stream){
    cursum2(isum, ints, stream);//sum of ints
    pywfs_grad_do<<<DIM(ints.Nx(), 256), 0, stream>>>
	(grad, ints, saa, isum, goff, gain, ints.Nx());
}
void pywfs_ints(curmat &ints, curmat &phiout, cuwfs_t &cuwfs, Real siglev, cudaStream_t stream){
    //Pyramid WFS
    cupowfs_t *cupowfs=cuwfs.powfs;
    PYWFS_T *pywfs=cupowfs->pywfs;
    cuzero(cuwfs.pypsf, stream);
    locfft_t *locfft=pywfs->locfft;
    const int nwvl=locfft->wvl->nx;
    const Real dx=locfft->loc->dx;
    const long nembed=locfft->nembed->p[0];
    const long nembed2=nembed/2;
    const long ncomp=pywfs->nominal->nx;
    const long ncomp2=ncomp/2;
    const Real pos_r=pywfs->modulate; 
    const int pos_n=pos_r>0?pywfs->modulpos:0;
    cucmat &otf=cuwfs.pyotf;
    for(int iwvl=0; iwvl<nwvl; iwvl++){
	cucmat &wvf=cuwfs.pywvf[iwvl];
	const Real alpha=pywfs->wvlwts->p[iwvl]/(ncomp*ncomp*pos_n);
	const Real wvl=locfft->wvl->p[iwvl];
	cuzero(wvf, stream);
	embed_wvf_do<<<DIM(phiout.Nx(),256),0,stream>>>
	    (wvf, phiout, cuwfs.amp, cupowfs->embed[iwvl], phiout.Nx(), wvl);
	CUFFT(cuwfs.plan_fs, wvf, CUFFT_FORWARD);
	fftshift_do<<<DIM2(wvf.Nx(), wvf.Ny(), 16),0,stream>>>
	    (wvf, wvf.Nx(), wvf.Ny());
	const Real otfnorm=1./(sqrt(locfft->ampnorm)*locfft->nembed->p[iwvl]);
	cucscale(wvf, otfnorm, stream);
	//cuwrite(wvf, "gpu_wvf0");
	const Real dtheta=locfft->wvl->p[iwvl]/(dx*nembed);
	for(int ipos=0; ipos<pos_n; ipos++){
	    const Real theta=2*M_PI*ipos/pos_n;
	    const Real posx=cos(theta)*pos_r;
	    const Real posy=sin(theta)*pos_r;
	    const long offy=(long)round(posy/dtheta);//offset of center
	    const long offy2=nembed2+offy-ncomp2;//offset of corner
	    const long iy0=MAX(-offy2, 0);
	    const long ny2=MIN(ncomp, nembed-offy2)-iy0;

	    const long offx=(long)round(posx/dtheta);
	    const long offx2=nembed/2+offx-ncomp2;
	    const long ix0=MAX(-offx2, 0);
	    const long nx2=MIN(ncomp, nembed-offx2)-ix0;
	    cuzero(otf, stream);
	    cwm_do<<<DIM2(nx2, ny2,16),0,stream>>>
		(otf.P()+ix0+iy0*ncomp, 
		 cupowfs->pyramid[iwvl].P()+ix0+iy0*ncomp, 
		 wvf.P()+ix0+offx2+(iy0+offy2)*nembed,
		 ncomp, nembed, nx2, ny2);
	    //cuwrite(otf, "gpu_wvf1_%d", ipos);
	    CUFFT(cuwfs.plan_py, otf, CUFFT_INVERSE);
	    //cuwrite(otf, "gpu_wvf2_%d", ipos);
	    curaddcabs2(cuwfs.pypsf, 1, otf, alpha, stream);
	    //cuwrite(cuwfs.pypsf, "gpu_wvf3_%d", ipos);
	}
	embed_do<<<DIM(ncomp*ncomp, 256),0,stream>>>
	    (otf, cuwfs.pypsf, ncomp*ncomp);
	//cuwrite(otf, "gpu_wvf4");
	CUFFT(cuwfs.plan_py, otf, CUFFT_FORWARD);
	//cuwrite(otf, "gpu_wvf5");
	cwm_do<<<DIM(ncomp*ncomp, 256), 0, stream>>>
	    (otf, cupowfs->pynominal, ncomp*ncomp);
	CUFFT(cuwfs.plan_py, otf, CUFFT_INVERSE);
	//cuwrite(otf, "gpu_wvf6");
	//Use ray tracing for si
	const Real dx2=dx*nembed/ncomp;
	const int nsa=cupowfs->saloc.Nloc();
	const Real scale=(Real)nsa*siglev/(Real)(ncomp*ncomp);

	for(int iy=0; iy<2; iy++){
	    for(int ix=0; ix<2; ix++){
		Real* pout=ints.P()+(ix+iy*2)*nsa;
		prop_linear<<<DIM(nsa, 256), 0, stream>>>
		    (pout, otf, otf.Nx(), otf.Ny(), 
		     cupowfs->saloc, cupowfs->saloc.Nloc(),
		     1./dx2, 1./dx2, 
		     (((ix-0.5)*ncomp2)-(-ncomp2+0.5)),
		     (((iy-0.5)*ncomp2)-(-ncomp2+0.5)), scale);
	    }
	}
	//cuwrite(ints, "gpu_ints"); exit(0);
    }
}
//dsp *gpu_pywfs_mkg(const PARMS_T *parms, const POWFS_T *powfs, loc_t *aloc, int iwfs, int idm){
dmat *gpu_pywfs_mkg(const PYWFS_T *pywfs, const loc_t* locin, const dmat *mod, double displacex, double displacey){
    gpu_set(cudata_t::wfsgpu[pywfs->iwfs0]);
    cuwfs_t &cuwfs=cudata->wfs[pywfs->iwfs0];
    cupowfs_t *cupowfs=cuwfs.powfs;
    stream_t &stream=cuwfs.stream;
    mapcell *mapinsq=(mapcell*)cellnew(1,1);
    dcell *opdin=dcellnew(1, 1);
    const Real siglev=100;//irrelevant in noise free case.
    for(int i=0; i<1; i++){
	mapinsq->p[i]=mapnew2(locin->map);
	opdin->p[i]=dnew(locin->nloc,1);
    }
    cumapcell cumapin(1,1);
    cp2gpu(cumapin, mapinsq);
    curmat phiout(pywfs->locfft->loc->nloc,1);
    culoc_t culocout(pywfs->locfft->loc); 
    const int nsa=cupowfs->saloc.Nloc();
    curmat ints(nsa,4);
    curmat grad(nsa*2,1);
    curmat grad0(nsa*2,1);
    cuzero(ints, stream);
    pywfs_ints(ints, phiout, cuwfs, siglev, stream);
    pywfs_grad(grad0, ints, cupowfs->saa, cuwfs.isum, cupowfs->pyoff, pywfs->gain, stream);
    TIC;tic;
    //cuwrite(grad0, "grad0_gpu");
    //cuwrite(ints, "ints0_gpu");
    const int nmod=mod?mod->ny:locin->nloc;
    dmat *ggd=dnew(nsa*2, nmod);
    for(int imod=0; imod<nmod; imod++){
	Real poke=pywfs->poke;
	if(mod){
	    dmat *tmp=drefcols(mod, imod, 1);
	    //double radial=ceil((sqrt(8.*(imod+1)+1)-3)*0.5)+1;
	    double tmax,tmin;
	    dmaxmin(tmp->p, tmp->nx, &tmax, &tmin);
	    poke/=(tmax-tmin);//sqrt(radial);
	    dadd(&opdin->p[0], 0, tmp, poke);
	    dfree(tmp);
	}else{
	    opdin->p[0]->p[imod]=poke;
	    if(imod>0){
		opdin->p[0]->p[imod-1]=0;
	    }
	}
	loc_embed(mapinsq->p[0], locin, opdin->p[0]->p);
	CUDA_SYNC_STREAM;
	cp2gpu(cumapin, mapinsq);
	cuzero(phiout, stream);
	gpu_dm2loc(phiout, culocout, cumapin, cumapin.Nx(), pywfs->hs, displacex, displacey, 0, 0, 1, stream);
	//cuwrite(cumapin[0].p, "gpu_cumapin_%d", imod);
	//cuwrite(phiout, "gpu_phiout_%d", imod);
	cuzero(ints, stream);
	pywfs_ints(ints, phiout, cuwfs, siglev, stream);
	//cuwrite(ints, "gpu_ints_%d", imod);
	pywfs_grad(grad, ints, cupowfs->saa, cuwfs.isum, cupowfs->pyoff, pywfs->gain,stream);
	curadd(grad, 1, grad0, -1, stream);
	curscale(grad, 1./poke, stream);
	dmat *gradc=drefcols(ggd, imod, 1);
	cp2cpu(&gradc, grad, stream);
	if(imod%10==0){
	    Real ts=myclockd()-tk;
	    info2("%d of %ld. %.2f of %.2f seconds. std(grad)=%g.\n", imod, locin->nloc, ts, ts/(imod+1)*locin->nloc, dstd(gradc));
	}
	dfree(gradc);
    }
    cellfree(opdin);
    cellfree(mapinsq);
    return ggd;
}
