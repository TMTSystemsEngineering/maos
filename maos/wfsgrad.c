/*
  Copyright 2009-2013 Lianqi Wang <lianqiw@gmail.com> <lianqiw@tmt.org>
  
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
#include "maos.h"
#include "sim.h"
#include "sim_utils.h"
#include "ahst.h"
#if USE_CUDA
#include "../cuda/gpu.h"
#endif
/**
   \file wfsgrad.c
   contains functions that computes WFS gradients in geometric or physical optics mode.
*/
#define TIMING 0
#if TIMING == 1
#define TIM(A) double tk##A=myclockd()
#else
#define TIM(A)
#endif

static void wfs_ideal_atm(SIM_T *simu, dmat *opd, int iwfs, double alpha){
    const PARMS_T *parms=simu->parms;
    POWFS_T *powfs=simu->powfs;
    const int ipowfs=parms->wfs[iwfs].powfs;
    const double hs=parms->powfs[ipowfs].hs;
    const int wfsind=parms->powfs[ipowfs].wfsind[iwfs];
   
    for(int idm=0; idm<parms->ndm; idm++){
	const double ht = parms->dm[idm].ht+parms->dm[idm].vmisreg;
	double dispx=ht*parms->wfs[iwfs].thetax+powfs[ipowfs].misreg[wfsind][0];
	double dispy=ht*parms->wfs[iwfs].thetay+powfs[ipowfs].misreg[wfsind][1];
	double scale=1.-ht/hs;
	if(parms->dm[idm].cubic){
	    prop_grid_cubic(simu->dmprojsq[idm],
			    powfs[ipowfs].loc, powfs[ipowfs].amp->p, opd->p, 
			    alpha, dispx, dispy, scale, parms->dm[idm].iac, 
			    0, 0);
	}else{
	    prop_grid(simu->dmprojsq[idm],
		      powfs[ipowfs].loc, powfs[ipowfs].amp->p, opd->p, 
		      alpha, dispx, dispy, scale, 0,
		      0, 0);
	}
    }
}
/**
   Compute the focus adjustment need to apply to OPD of wfs. Used in both CPU and GPU code.
*/
double wfsfocusadj(SIM_T *simu, int iwfs){
    const PARMS_T *parms=simu->parms;
    const POWFS_T *powfs=simu->powfs;
    const int ipowfs=parms->wfs[iwfs].powfs;
    const int wfsind=parms->powfs[ipowfs].wfsind[iwfs];
    const int isim=simu->isim;
    double focus=0;
    if(powfs[ipowfs].focus){
	const long nx=powfs[ipowfs].focus->nx;
	focus+=powfs[ipowfs].focus->p[(isim%nx)+nx*(powfs[ipowfs].focus->ny==parms->powfs[ipowfs].nwfs?wfsind:0)];
    }
    if(simu->zoomint && simu->zoomint->p[iwfs]){
	focus-=simu->zoomint->p[iwfs]->p[0];
	simu->zoompos->p[iwfs]->p[isim]=simu->zoomint->p[iwfs]->p[0];
    }
    if(parms->sim.ahstfocus==3 && simu->Mint_lo->mint[0]){
	/*In new ahst mode, the first plate scale mode contains focus for
	  lgs. But it turns out to be not necessary to remove it because the
	  HPF in the LGS path removed the influence of this focus mode. set
	  sim.ahstfocus=3 to enable adjust opd directly (fake).*/
	double scale=simu->recon->ngsmod->scale;
	focus-=simu->Mint_lo->mint[0]->p[0]->p[2]*(scale-1);
    }
    return focus;
}

void wfslinearity(const PARMS_T *parms, POWFS_T *powfs, const int iwfs){
    const int ipowfs=parms->wfs[iwfs].powfs;
    const int wfsind=parms->powfs[ipowfs].wfsind[iwfs];
    const int nwvl=parms->powfs[ipowfs].nwvl;
    const int nsa=powfs[ipowfs].pts->nsa;
    INTSTAT_T *intstat=powfs[ipowfs].intstat;
    ccell *fotf=intstat->fotf[intstat->nsepsf>1?wfsind:0];
    ccell *otf=ccellnew(nwvl,1);
    for(int iwvl=0; iwvl<nwvl; iwvl++){
	otf->p[iwvl]=cnew(fotf->p[0]->nx, fotf->p[0]->ny);
	cfft2plan(otf->p[iwvl], 1);
	cfft2plan(otf->p[iwvl], -1);
    }
    double pixthetax=parms->powfs[ipowfs].radpixtheta;
    double pixthetay=parms->powfs[ipowfs].pixtheta;
    dmat **mtche=NULL;
    if(parms->powfs[ipowfs].phytype==1){
	if(powfs[ipowfs].intstat->mtche->ny==1){
	    mtche=powfs[ipowfs].intstat->mtche->p;
	}else{
	    mtche=powfs[ipowfs].intstat->mtche->p+nsa*wfsind;
	}
    }
    double *srot=NULL;
    if(parms->powfs[ipowfs].radpix){
	srot=powfs[ipowfs].srot->p[powfs[ipowfs].srot->ny>1?wfsind:0]->p;
    }

    const int nsep=20;
    const double dg=0.1;
    double gx=0, gy=0, dgx=0, dgy=0;
    dmat *ints=dnew(powfs[ipowfs].pixpsax, powfs[ipowfs].pixpsay);
    double theta=0, cx=1, sx=0;
    dmat *gnf=dnew(nsep,nsa*2);
    PDMAT(gnf,pgnf);
    char *dirs[]={"x", "y", "diag"};
    char *types[]={"","MF", "CoG", "MAP"};
    if(parms->powfs[ipowfs].mtchcr){
	types[1]="MFC";
    }
    int radrot=parms->powfs[ipowfs].radrot;
    int type=parms->powfs[ipowfs].phytypesim;
    for(int dir=0; dir<3; dir++){
	dzero(gnf);
	for(int isa=0; isa<nsa; isa++){
	    switch(dir){
	    case 0:
		dgx=dg*pixthetax;
		dgy=0;
		break;
	    case 1:
		dgx=0;
		dgy=dg*pixthetay;
		break;
	    case 2:
		dgx=sqrt(0.5)*dg*pixthetax;
		dgy=sqrt(0.5)*dg*pixthetay;
		break;
	    }
	    if(srot){
		theta=srot[isa];
		cx=cos(theta);
		sx=sin(theta);
	    }
	    if(srot && !radrot){/*rot the vector from r/a to x/y*/
		double tmp=dgx*cx-dgy*sx;
		dgy=dgx*sx+dgy*cx;
		dgx=tmp;
	    }
	    for(int isep=0; isep<nsep; isep++){
		gx=dgx*isep;
		gy=dgy*isep;
		dzero(ints);
		for(int iwvl=0; iwvl<nwvl; iwvl++){
		    double wvlsig=parms->wfs[iwfs].wvlwts[iwvl]
			*parms->wfs[iwfs].siglev*parms->powfs[ipowfs].dtrat;
		    int idtf=powfs[ipowfs].dtf[iwvl].si->ny>1?wfsind:0;
		    int idtfsa=powfs[ipowfs].dtf[iwvl].si->nx>1?isa:0;
		    PDSPCELL(powfs[ipowfs].dtf[iwvl].si, psi);
		    dsp *sis=psi[idtf][idtfsa];
		    double wvl=parms->powfs[ipowfs].wvl[iwvl];
		    double dtheta1=powfs[ipowfs].pts->nx*powfs[ipowfs].pts->dx*parms->powfs[ipowfs].embfac/wvl;
		    ctilt2(otf->p[iwvl], fotf->p[isa+nsa*iwvl], gx*dtheta1, gy*dtheta1, 0);
		    cfft2(otf->p[iwvl], 1);
		    spmulcreal(ints->p, sis, otf->p[iwvl]->p, wvlsig);
		}
		//ddraw("ints", ints, NULL, NULL, "ints", "x", "y", "ints"); PAUSE;
		double g[3]={gx+pixthetax*0.1,gy+pixthetay*0.1,1};
		switch(type){
		case 1:{/*(constraint) Matched filter*/
		    dmulvec(g, mtche[isa], ints->p,1.);
		}
		    break;
		case 2:{/*tCoG*/
		    double pmax=1;
		    dcog(g,ints,0.,0.,parms->powfs[ipowfs].cogthres*pmax,parms->powfs[ipowfs].cogoff*pmax);
		    g[0]*=pixthetax;
		    g[1]*=pixthetay;
		}
		    break;
		case 3:{/*MAP*/
		    maxapriori(g, ints, parms, powfs, iwfs, isa, 1, 0, 1);
		}
		    break;
		default:
		    error("Invalid");
		}
		if(srot){/*rotate from xy to r/a*/
		    double tmp=g[0]*cx+g[1]*sx;
		    g[1]=-g[0]*sx+g[1]*cx;
		    g[0]=tmp;
		}
		pgnf[isa][isep]=g[0]/pixthetax;
		pgnf[isa+nsa][isep]=g[1]/pixthetay;
	    }
	}/*for isa*/
	dwrite(gnf, "wfslinearity_wfs%d_%s_%s", iwfs, types[type],dirs[dir]);
    }
    dfree(ints);
    ccellfree(otf);
}

typedef struct {
    const PARMS_T *parms;
    const POWFS_T *powfs;
    dmat *ints;
    ccell *fotf;
    ccell *otf;//temporary.
    double bkgrnd;
    double rne;
    int noisy;
    int iwfs;
    int isa;
}mapdata_t;
/**
  The function to evaluate the result at x.
*/
static double mapfun(double *x, mapdata_t *info){
    dmat *ints=info->ints;
    ccell *fotf=info->fotf;
    ccell *otf=info->otf;
    const PARMS_T *parms=info->parms;
    const POWFS_T *powfs=info->powfs;
    int iwfs=info->iwfs;
    int ipowfs=parms->wfs[iwfs].powfs;
    int wfsind=parms->powfs[ipowfs].wfsind[iwfs];
    int isa=info->isa;
    int nsa=fotf->nx;
    int nwvl=fotf->ny;
    if(!otf){
	info->otf=ccellnew(nwvl,1);
	for(int iwvl=0; iwvl<nwvl; iwvl++){
	    info->otf->p[iwvl]=cnew(info->fotf->p[0]->nx, info->fotf->p[0]->ny);
	    cfft2plan(info->otf->p[iwvl], 1);
	    cfft2plan(info->otf->p[iwvl], -1);
	}
	otf=info->otf;
    }
    dmat *ints2=dnew(ints->nx, ints->ny);
    for(int iwvl=0; iwvl<nwvl; iwvl++){
	double wvlsig=parms->wfs[iwfs].wvlwts[iwvl]
	    *parms->wfs[iwfs].siglev*parms->powfs[ipowfs].dtrat;
	PDSPCELL(powfs[ipowfs].dtf[iwvl].si, psi);
	int idtf=powfs[ipowfs].dtf[iwvl].si->ny>1?wfsind:0;
	int idtfsa=powfs[ipowfs].dtf[iwvl].si->nx>1?isa:0;
	dsp *sis=psi[idtf][idtfsa];
	double wvl=parms->powfs[ipowfs].wvl[iwvl];
	double dtheta1=powfs[ipowfs].pts->nx*powfs[ipowfs].pts->dx*parms->powfs[ipowfs].embfac/wvl;
	ctilt2(info->otf->p[iwvl], info->fotf->p[isa+nsa*iwvl], x[0]*dtheta1, x[1]*dtheta1, 0);
	cfft2(info->otf->p[iwvl], 1);
	spmulcreal(ints2->p, sis, info->otf->p[iwvl]->p, wvlsig*x[2]);
    }
 
    double sigma=0;
    if(info->noisy){
	double noise=info->rne*info->rne+info->bkgrnd;
	for(int i=0; i<ints->nx*ints->ny; i++){
	    sigma+=pow(ints->p[i]-ints2->p[i],2)/(ints2->p[i]+noise);
	}
    }else{
	for(int i=0; i<ints->nx*ints->ny; i++){
	    sigma+=pow(ints->p[i]-ints2->p[i],2);
	}
    }
    /*info("Map fun called with [%g %g] %g, sigma=%g. noisy=%d\n", x[0], x[1], x[2], sigma, info->noisy);*/
    dfree(ints2);
    return sigma;
}
/**
   Implements MAP tracking algorithm. The polar coordinate is implicitly taken care of in mapfun if parms->powfs.radrot=0;
*/
void maxapriori(double *g, dmat *ints, const PARMS_T *parms, 
		const POWFS_T *powfs, int iwfs, int isa, int noisy,
		double bkgrnd, double rne){
    int ipowfs=parms->wfs[iwfs].powfs;
    int wfsind=parms->powfs[ipowfs].wfsind[iwfs];
    double pixthetax=parms->powfs[ipowfs].radpixtheta;
    double pixthetay=parms->powfs[ipowfs].pixtheta;
    INTSTAT_T *intstat=powfs[ipowfs].intstat;
    ccell *fotf=intstat->fotf[intstat->nsepsf>1?wfsind:0];
    mapdata_t data={parms, powfs, ints, fotf, NULL, bkgrnd, rne, noisy, iwfs, isa};
    double scale[3]={0.1*pixthetax, 0.1*pixthetay, 0.1};
    //info2("isa %d: %.4e %.4e %.2f", isa, g[0], g[1], g[2]);
    int ncall=dminsearch(g, scale, 3, MIN(pixthetax, pixthetay)*1e-2, (minsearch_fun)mapfun, &data);
    ccellfree(data.otf);
    /* convert to native format along x/y or r/a to check for overflow*/
    if(parms->powfs[ipowfs].radpix && !parms->powfs[ipowfs].radrot){
	double theta=powfs[ipowfs].srot->p[powfs[ipowfs].srot->ny>1?wfsind:0]->p[isa];
	double cx=cos(theta);
	double sx=sin(theta);
	double tmp=g[0]*cx+g[1]*sx;
	g[1]=-g[0]*sx+g[1]*cx;
	g[0]=tmp;
    }
    double gx=g[0]/pixthetax*2./ints->nx;
    double gy=g[1]/pixthetay*2./ints->ny;
    if(fabs(gx)>0.55||fabs(gy)>0.55){
	warning2("sa %4d iter %3d: wrapped: gx=%6.3f, gy=%6.3f ==> ", isa, ncall, gx, gy);
	gx=gx-floor(gx+0.5);
	gy=gy-floor(gy+0.5);
	warning2("gx=%6.3f, gy=%6.3f\n", gx, gy);
	g[0]=pixthetax*ints->nx/2*gx;
	g[1]=pixthetay*ints->ny/2*gy;
    }
    //info2("==> %.4e %.4e %.2f after %d iter\n", g[0], g[1], g[2], ncall);
    if(parms->powfs[ipowfs].radpix){
	double theta=powfs[ipowfs].srot->p[powfs[ipowfs].srot->ny>1?wfsind:0]->p[isa];
	double cx=cos(theta);
	double sx=sin(theta);
	double tmp=g[0]*cx-g[1]*sx;
	g[1]=g[0]*sx+g[1]*cx;
	g[0]=tmp;
    }
}
/**
   computes close loop and pseudo open loop gradidents for both gometric and
   physical optics WFS. Calls wfsints() to accumulate WFS subapertures images in
   physical optics mode.  */

void wfsgrad_iwfs(SIM_T *simu, int iwfs){
    const PARMS_T *parms=simu->parms;
    assert(iwfs<parms->nwfs);
    /*
      simu->gradcl is CL grad output (also for warm-restart of maxapriori
      simu->gradacc is internal, to accumulate geometric grads.
      do not accumulate opd. accumate ints for phy, g for GS
    */
    /*input */
    
    map_t **atm=simu->atm;
    const RECON_T *recon=simu->recon;
    const POWFS_T *powfs=simu->powfs;
    /*output */
    const int CL=parms->sim.closeloop;
    const int isim=simu->isim;
    const int nps=parms->atm.nps;
    const double dt=simu->dt;
    TIM(0);
    /*The following are truly constants for this powfs */
    const int ipowfs=parms->wfs[iwfs].powfs;
    const int imoao=parms->powfs[ipowfs].moao;
    const int nsa=powfs[ipowfs].pts->nsa;
    const int pixpsa=powfs[ipowfs].pts->nx*powfs[ipowfs].pts->nx;
    const int wfsind=parms->powfs[ipowfs].wfsind[iwfs];
    const double hs=parms->powfs[ipowfs].hs;
    const int npix=pixpsa*nsa;
    const int dtrat=parms->powfs[ipowfs].dtrat;
    const int save_gradgeom=parms->save.gradgeom[iwfs];
    const int save_opd =parms->save.wfsopd[iwfs];
    const int save_ints=parms->save.ints[iwfs];
    const int noisy=parms->powfs[ipowfs].noisy;
    const int nthread=powfs[ipowfs].nthread;
    /*The following depends on isim */
    /*const int dtrat_reset=(isim%dtrat==0); */
    const int dtrat_output=((isim+1)%dtrat==0);
    const int do_phy=(parms->powfs[ipowfs].usephy && isim>=parms->powfs[ipowfs].phystep);
    const int do_pistatout=parms->powfs[ipowfs].pistatout&&isim>=parms->powfs[ipowfs].pistatstart;
    const int do_geom=!do_phy || save_gradgeom || do_pistatout;
    const double *realamp=powfs[ipowfs].realamp[wfsind];
    double *srot=(do_phy && parms->powfs[ipowfs].radpix)?
	powfs[ipowfs].srot->p[powfs[ipowfs].srot->ny>1?wfsind:0]->p:NULL;
    dmat *gradcalc=NULL;
    dmat **gradacc=&simu->gradacc->p[iwfs];
    dmat **gradout=&simu->gradcl->p[iwfs];
    dcell *ints=simu->ints[iwfs];
    dmat  *opd=dnew(npix,1);

    if(simu->telws){/*Wind shake */
	double tmp=simu->telws->p[isim];
	double angle=simu->winddir?simu->winddir->p[0]:0;
	double ptt[3]={0, tmp*cos(angle), tmp*sin(angle)};
	loc_add_ptt(opd->p, ptt, powfs[ipowfs].loc);
    }

    /* Add surface error*/
    if(powfs[ipowfs].opdadd && powfs[ipowfs].opdadd->p[wfsind]){
	dadd(&opd,1, powfs[ipowfs].opdadd->p[wfsind],1);
    }

    /* Now begin ray tracing. */
    if(parms->sim.idealwfs && !parms->powfs[ipowfs].lo){
	wfs_ideal_atm(simu, opd, iwfs, 1);
    }else if(atm){
	for(int ips=0; ips<nps; ips++){
	    thread_t *wfs_prop=simu->wfs_prop_atm[iwfs+parms->nwfs*ips];
	    PROPDATA_T *wfs_propdata=&simu->wfs_propdata_atm[iwfs+parms->nwfs*ips];
	    wfs_propdata->phiout=opd->p;
	    wfs_propdata->displacex1=-atm[ips]->vx*dt*isim;
	    wfs_propdata->displacey1=-atm[ips]->vy*dt*isim;
	    /* have to wait to finish before another phase screen. */
	    CALL_THREAD(wfs_prop, nthread, 0);
	}
	/* most expensive 0.10 per LGS for*/
	if(parms->sim.wfsalias){
	    /* Remove subspace of atm projected onto range of DM.*/
	    wfs_ideal_atm(simu, opd, iwfs, -1);
	}
    }
    if(save_opd){
	cellarr_dmat(simu->save->wfsopdol[iwfs], isim, opd);
    }
 
    if(CL){
	for(int idm=0; idm<parms->ndm; idm++){
	    thread_t *wfs_prop=simu->wfs_prop_dm[iwfs+parms->nwfs*idm];
	    PROPDATA_T *wfs_propdata=&simu->wfs_propdata_dm[iwfs+parms->nwfs*idm];
	    wfs_propdata->phiout=opd->p;
	    CALL_THREAD(wfs_prop, nthread, 0);
	}/*idm */
    }
    if(parms->powfs[ipowfs].skip && parms->tomo.ahst_idealngs){
	//apply ideal NGS modes to NGS WFS
	ngsmod2science(opd, powfs[ipowfs].loc, recon->ngsmod, 
		       parms->wfs[iwfs].thetax, parms->wfs[iwfs].thetay,
		       simu->cleNGSm->p+isim*recon->ngsmod->nmod, -1);
    }
    if(imoao>-1){
	dmat **dmwfs=simu->dm_wfs->p;
	if(dmwfs[iwfs]){
	    //info("iwfs %d: Adding MOAO correction\n", iwfs);
	    /* No need to do mis registration here since the MOAO DM is attached
	       to close to the WFS.*/
	    if(parms->moao[imoao].cubic){
		prop_nongrid_pts_cubic(recon->moao[imoao].aloc, dmwfs[iwfs]->p,
				       powfs[ipowfs].pts, realamp, opd->p, -1, 0, 0, 1, 
				       parms->moao[imoao].iac, 0, 0);
	    }else{
		prop_nongrid_pts(recon->moao[imoao].aloc, dmwfs[iwfs]->p,
				 powfs[ipowfs].pts, realamp, opd->p, -1, 0, 0, 1, 
				 0, 0);
	    }
	}
    }
    /* Add defocus to OPD if needed. */
    if(parms->powfs[ipowfs].llt){
	double focus=wfsfocusadj(simu, iwfs);
	if(fabs(focus)>1e-20){
	    loc_add_focus(opd->p, powfs[ipowfs].loc, focus);
	}
    }
    if(parms->powfs[ipowfs].fieldstop>0){
	apply_fieldstop(opd, powfs[ipowfs].amp, powfs[ipowfs].embed, powfs[ipowfs].nembed, 
			powfs[ipowfs].fieldstop, parms->powfs[ipowfs].wvl[0]);
    }

    if(save_opd){
	cellarr_dmat(simu->save->wfsopd[iwfs], isim, opd);
    }
    if(parms->plot.run){
	drawopdamp("wfsopd",powfs[ipowfs].loc,opd->p,realamp,NULL,
		   "WFS OPD","x (m)", "y (m)", "WFS %d", iwfs);
    }
    if(do_geom){
	/* Now Geometric Optics gradient calculations. if dtrat==1, we compute
	   gradients directly to gradacc, which is the same as gradcalc. If
	   dtrat>1, we compute gradients to gradcalc, and accumulate to
	   gradacc. gradcalc is used to shift pistat. We DONOT include gradoff
	   adjustment to gradref, but only do it on gradcl. This will make the
	   pistat always peak in center no matter what NCPA is present.
	*/
	if(!do_pistatout || parms->powfs[ipowfs].pistatstc || dtrat==1){
	    gradcalc=dref(*gradacc);
	}//else: calculate first to gradcalc then add to gradacc
	if(parms->powfs[ipowfs].gtype_sim==1){ /*compute ztilt. */
	    pts_ztilt(&gradcalc,powfs[ipowfs].pts,
		      powfs[ipowfs].saimcc[powfs[ipowfs].nsaimcc>1?wfsind:0], 
		      realamp, opd->p);
	}else{/*G tilt */
	    spmulmat(&gradcalc,adpind(powfs[ipowfs].GS0,wfsind),opd,1);
	}
	if(powfs[ipowfs].gradoff){
	    dadd(&gradcalc, 1, powfs[ipowfs].gradoff->p[wfsind], -1);
	}
	if(gradcalc->p!=(*gradacc)->p){
	    dadd(gradacc, 1, gradcalc, 1);
	}
    }

    ccell *psfout=NULL;
    cellarr *psfoutcellarr=NULL;
    cellarr *ztiltoutcellarr=NULL;
    if(parms->powfs[ipowfs].psfout){
	psfout=simu->wfspsfout[iwfs];
	psfoutcellarr=simu->save->wfspsfout[iwfs];
	ztiltoutcellarr=simu->save->ztiltout[iwfs];
    }
    TIM(1);
    /* Now begin Physical Optics Intensity calculations */
    if(do_phy || psfout || do_pistatout){
	dmat *lltopd=NULL;
	if(powfs[ipowfs].llt && parms->powfs[ipowfs].trs){
	    if(powfs[ipowfs].llt->ncpa){
		int iotf=powfs[ipowfs].llt->ncpa->nx==1?0:wfsind;
		lltopd=ddup(powfs[ipowfs].llt->ncpa->p[iotf]);
	    }else{
		lltopd=dnew(powfs[ipowfs].llt->pts->nx,
			    powfs[ipowfs].llt->pts->nx);
	    }
	    const int illt=parms->powfs[ipowfs].llt->i[wfsind];
	    if(atm){/*LLT OPD */
		for(int ips=0; ips<nps; ips++){
		    const double hl=atm[ips]->h;
		    const double scale=1.-hl/hs;
		    const double thetax=parms->wfs[iwfs].thetax-parms->powfs[ipowfs].llt->ox[illt]/hs;
		    const double thetay=parms->wfs[iwfs].thetay-parms->powfs[ipowfs].llt->oy[illt]/hs;
		    const double displacex=-atm[ips]->vx*isim*dt+thetax*hl+parms->powfs[ipowfs].llt->misreg[0];
		    const double displacey=-atm[ips]->vy*isim*dt+thetay*hl+parms->powfs[ipowfs].llt->misreg[1];
		    prop_grid_pts(atm[ips],powfs[ipowfs].llt->pts,NULL,
				  lltopd->p,1,displacex,displacey,
				  scale, 1., 0, 0);
		}
	    }
	    double ttx=0, tty=0;//FSM + wind shake induced jitter
	    if((simu->uptreal && simu->uptreal->p[iwfs]) ||do_pistatout||parms->sim.uptideal){
		if(do_pistatout||parms->sim.uptideal){
		    /* remove tip/tilt completely */
		    dmat *lltg=dnew(2,1);
		    pts_ztilt(&lltg,powfs[ipowfs].llt->pts,
			      powfs[ipowfs].llt->imcc,
			      powfs[ipowfs].llt->amp->p,
			      lltopd->p);
		    ttx=-lltg->p[0];
		    tty=-lltg->p[1];
		    dfree(lltg);
		}else{
		    ttx=simu->uptreal->p[iwfs]->p[0];
		    tty=simu->uptreal->p[iwfs]->p[1];
		}
		/* copy uptreal to output */
		PDMAT(simu->uptcmds->p[iwfs], puptcmds);
		puptcmds[isim][0]=ttx;
		puptcmds[isim][1]=tty;
	    }
	    if(simu->telws){
		double tmp=simu->telws->p[isim];
		double angle=simu->winddir?simu->winddir->p[0]:0;
		ttx+=tmp*cos(angle)*parms->powfs[ipowfs].llt->ttrat;
		tty+=tmp*sin(angle)*parms->powfs[ipowfs].llt->ttrat;
	    }
	    if(ttx !=0 || tty != 0){ /* add tip/tilt to llt opd */
		double ptt[3]={0, ttx, tty};
		loc_add_ptt(lltopd->p, ptt, powfs[ipowfs].llt->loc);
	    }
	    if(save_opd){
		cellarr_dmat(simu->save->wfslltopd[iwfs], isim, lltopd);
	    }
	}
	WFSINTS_T *intsdata=simu->wfs_intsdata+iwfs;
	intsdata->ints=ints;
	intsdata->psfout=psfout;
	intsdata->pistatout=simu->pistatout[iwfs];
	intsdata->gradref=gradcalc;
	intsdata->opd=opd;
	intsdata->lltopd=lltopd;
	CALL_THREAD(simu->wfs_ints[iwfs], nthread, 0);
	dfree(lltopd);
	if(psfout){
	    cellarr_ccell(psfoutcellarr, isim, psfout);
	    cellarr_dmat(ztiltoutcellarr, isim, *gradacc);
	}
    }
    TIM(2);

    dfree(opd);

    if(dtrat_output){
	if(do_phy){
	    /* In Physical optics mode, do integration and compute
	       gradients. The matched filter are in x/y coordinate even if
	       radpix=1. */
	    dmat **mtche=NULL;
	    double *i0sum=NULL;
	    
	    if(parms->powfs[ipowfs].phytype==1){
		if(powfs[ipowfs].intstat->mtche->ny==1){
		    mtche=powfs[ipowfs].intstat->mtche->p;
		    i0sum=powfs[ipowfs].intstat->i0sum->p;
		}else{
		    mtche=powfs[ipowfs].intstat->mtche->p+nsa*wfsind;
		    i0sum=powfs[ipowfs].intstat->i0sum->p+nsa*wfsind;
		}
	    }
	    double pixthetax=parms->powfs[ipowfs].radpixtheta;
	    double pixthetay=parms->powfs[ipowfs].pixtheta;
	    /*Rayleigh scattering (bkgrnd) */
	    dmat **bkgrnd2=NULL;
	    dmat **bkgrnd2c=NULL;
	    if(noisy){/*add noise */
		if(powfs[ipowfs].bkgrnd){
		    if(powfs[ipowfs].bkgrnd->ny==1){
			bkgrnd2=powfs[ipowfs].bkgrnd->p;
		    }else{
			bkgrnd2=powfs[ipowfs].bkgrnd->p+nsa*wfsind;
		    }
		}
		if(powfs[ipowfs].bkgrndc){
		    if(powfs[ipowfs].bkgrndc->ny==1){
			bkgrnd2c=powfs[ipowfs].bkgrndc->p;
		    }else{
			bkgrnd2c=powfs[ipowfs].bkgrndc->p+nsa*wfsind;
		    }
		}
	    }
	    /*output directly to simu->gradcl. replace */
	    const double rne=parms->powfs[ipowfs].rne;
	    const double bkgrnd=parms->powfs[ipowfs].bkgrnd*dtrat;
	    double *pgradx=(*gradout)->p;
	    double *pgrady=pgradx+nsa;
	    if(save_ints){
		cellarr_dcell(simu->save->intsnf[iwfs], isim, ints);
	    }
	    for(int isa=0; isa<nsa; isa++){
		/* TODO: Do something to remove negative pixels. shift image or
		   mask out. This is important when bkgrndfnc is greater than
		   1. */
		/*the third element is strength, for maxapriori.*/
		double geach[3]={0,0,1};
		if(noisy){/*add noise */
		    double *bkgrnd2i=(bkgrnd2 && bkgrnd2[isa])?bkgrnd2[isa]->p:NULL;
		    double *bkgrnd2ic=(bkgrnd2c && bkgrnd2c[isa])?bkgrnd2c[isa]->p:NULL;
		    addnoise(ints->p[isa], &simu->wfs_rand[iwfs],
			     bkgrnd,parms->powfs[ipowfs].bkgrndc,
			     bkgrnd2i, bkgrnd2ic, rne);
		}
		switch(parms->powfs[ipowfs].phytypesim){
		case 1:{
		    dmulvec(geach, mtche[isa],ints->p[isa]->p,1.);
		    if(parms->powfs[ipowfs].mtchscl){
			double scale=i0sum[isa]/dsum(ints->p[isa]);
			geach[0]*=scale;
			geach[1]*=scale;
		    }
		}
		    break;
		case 2:{
		    double pmax=sqrt(rne*rne+bkgrnd);
		    dcog(geach,ints->p[isa],0.,0.,parms->powfs[ipowfs].cogthres*pmax,
			 parms->powfs[ipowfs].cogoff*pmax);
		    geach[0]*=pixthetax;
		    geach[1]*=pixthetay;
		    if(srot){
			double theta=srot[isa];
			double cx=cos(theta);
			double sx=sin(theta);
			double tmp=geach[0]*cx-geach[1]*sx;
			geach[1]=geach[0]*sx+geach[1]*cx;
			geach[0]=tmp;
		    }
		}
		    break;
		case 3:{
		    geach[0]=pgradx[isa];//warm restart
		    geach[1]=pgrady[isa];
		    maxapriori(geach, ints->p[isa], parms, powfs, iwfs, isa, 1, bkgrnd, rne);
		}
		    break;
		default:
		    error("Invalid");
		}
	
		pgradx[isa]=geach[0];
		pgrady[isa]=geach[1];
	    };/*isa */
	
	    if(save_ints){
		cellarr_dcell(simu->save->intsny[iwfs], isim, ints);
	    }
	    dcellzero(simu->ints[iwfs]);
	}else{
	    /* geomtric optics accumulation mode. scale and copy results to output. */
	    dcp(gradout,*gradacc);
	    if(dtrat!=1){
		dscale(*gradout,1./dtrat);/*average */
	    }
	    if(noisy && !parms->powfs[ipowfs].usephy){
		const dmat *nea=powfs[ipowfs].neasim->p[wfsind];
		const double *neax=nea->p;
		const double *neay=nea->p+nsa;
		const double *neaxy=nea->p+nsa*2;
		double *restrict ggx=(*gradout)->p;
		double *restrict ggy=(*gradout)->p+nsa;
		for(int isa=0; isa<nsa; isa++){
		    /*Preserve the random sequence. */
		    double n1=randn(&simu->wfs_rand[iwfs]);
		    double n2=randn(&simu->wfs_rand[iwfs]);
		    double errx=neax[isa]*n1;
		    double erry=neay[isa]*n2+neaxy[isa]*n1;/*cross term. */
		    ggx[isa]+=errx;
		    ggy[isa]+=erry;
		}
	    }
	    dzero(*gradacc);
	}
	if(save_gradgeom){
	    dmat *gradtmp=NULL;
	    dadd(&gradtmp, 1, *gradacc, 1./dtrat);
	    cellarr_dmat(simu->save->gradgeom[iwfs], isim, gradtmp);/*noise free. */
	    dfree(gradtmp);
	}
    }//dtrat_out
    dfree(gradcalc);
    TIM(3);
#if TIMING==1
    info("wfs %d grad timing: ray %.2f ints %.2f grad %.2f\n",iwfs,tk1-tk0,tk2-tk1,tk3-tk2);
#endif
}
/**
   Save telemetry. TODO: copy from GPU to CPU.
*/
static void wfsgrad_save(SIM_T *simu){
    const PARMS_T *parms=simu->parms;
    const int isim=simu->isim;
    if(isim+1==parms->sim.end){
	for(int iwfs=0; iwfs<simu->parms->nwfs; iwfs++){
	    const int ipowfs=simu->parms->wfs[iwfs].powfs;
	    const int dtrat=parms->powfs[ipowfs].dtrat;
	    double scale;
	    if(parms->powfs[ipowfs].usephy){
		scale=(simu->isim+1-simu->parms->powfs[ipowfs].phystep)/dtrat;
	    }else{
		scale=(simu->isim+1)/dtrat;
	    }
	    if(scale<=0) continue;	    
	    if(simu->pistatout && simu->pistatout[iwfs]){
		int nstep=isim+1-parms->powfs[ipowfs].pistatstart;
		scale=1./(double)nstep;
		dcell *pp=simu->pistatout[iwfs];
		dcellscale(pp,scale);
		if(parms->sim.skysim){/*need peak in corner */
		    for(long ic=0; ic<pp->nx*pp->ny; ic++){
			dfftshift(pp->p[ic]);
		    }
		    dcellwrite(pp,"%s/pistat/pistat_seed%d_sa%d_x%g_y%g.bin",
			       dirskysim,simu->seed,
			       parms->powfs[ipowfs].order,
			       parms->wfs[iwfs].thetax*206265,
			       parms->wfs[iwfs].thetay*206265);
		    for(long ic=0; ic<pp->nx*pp->ny; ic++){
			dfftshift(pp->p[ic]);
		    }
		}else{/*need peak in center */
		    dcellwrite(pp,"pistat_seed%d_wfs%d.bin", simu->seed,iwfs);
		}
		dcellscale(pp,1./scale);
	    }
	}
    }
}
void wfsgrad_wrap(thread_t *info){
    SIM_T *simu=(SIM_T*)info->data;
    const PARMS_T *parms=simu->parms;
    int iwfs=info->start;
    assert(info->end==info->start+1);/*only 1 WFS. */
#if USE_CUDA
    if(parms->gpu.wfs){
	gpu_wfsgrad_iwfs(simu, iwfs);
    }else
#endif
	{
	    wfsgrad_iwfs(simu, iwfs);
	}

    //Postprocessing gradients
    const RECON_T *recon=simu->recon;
    const POWFS_T *powfs=simu->powfs;
    const int nwfs=parms->nwfs;
    const int isim=simu->isim;
    const int ipowfs=parms->wfs[iwfs].powfs;
    const int wfsind=parms->powfs[ipowfs].wfsind[iwfs];
    const int dtrat=parms->powfs[ipowfs].dtrat;
    const int dtrat_output=((isim+1)%dtrat==0);
    const int do_phy=(parms->powfs[ipowfs].usephy && isim>=parms->powfs[ipowfs].phystep);
    dmat **gradout=&simu->gradcl->p[iwfs];
    if(dtrat_output){
	if(do_phy){
	    if(parms->powfs[ipowfs].llt && parms->powfs[ipowfs].trs){
		if(!recon->PTT){
		    error("powfs %d has llt, but recon->PTT is NULL",ipowfs);
		}
		dmat *PTT=NULL;
		if(parms->recon.glao){
		    PTT=recon->PTT->p[ipowfs+ipowfs*parms->npowfs];
		}else{
		    PTT=recon->PTT->p[iwfs+iwfs*nwfs];
		}
		if(!PTT){
		    error("powfs %d has llt, but TT removal is empty\n", ipowfs);
		}
		/* Compute LGS Uplink error. */
		dzero(simu->upterr->p[iwfs]);
		dmm(&simu->upterr->p[iwfs], PTT, *gradout, "nn", 1);
		/* copy upterr to output. */
		PDMAT(simu->upterrs->p[iwfs], pupterrs);
		pupterrs[isim][0]=simu->upterr->p[iwfs]->p[0];
		pupterrs[isim][1]=simu->upterr->p[iwfs]->p[1];
	    }
	    if(powfs[ipowfs].gradphyoff){
		dadd(gradout, 1, powfs[ipowfs].gradphyoff->p[wfsind], -1);
	    }
	
	}
	if(powfs[ipowfs].gradoff){
	    dadd(gradout, 1, powfs[ipowfs].gradoff->p[wfsind], -1);
	}
	if(parms->powfs[ipowfs].llt && parms->sim.ahstfocus==2 && simu->Mint_lo->mint[0]){
	    /*In new ahst mode, the first plate scale mode contains focus for
	      lgs. But it turns out to be not necessary to remove it because the
	      HPF in the LGS path removed the influence of this focus mode. set
	      sim.ahstfocus=2 to enable adjust gradients.*/
	    double scale=simu->recon->ngsmod->scale;
	    dmat *focus=dnew(1,1);
	    focus->p[0]=-simu->Mint_lo->mint[0]->p[0]->p[2]*(scale-1);
	    dmm(gradout, recon->GFall->p[iwfs], focus, "nn", 1);
	    dfree(focus);
	}
	if(parms->save.grad[iwfs]){
	    cellarr_dmat(simu->save->gradcl[iwfs], isim, simu->gradcl->p[iwfs]);
	}
	if(parms->plot.run){
	    drawopd("Gclx",(loc_t*)powfs[ipowfs].pts, simu->gradcl->p[iwfs]->p, NULL,
		    "WFS Closeloop Gradients (x)","x (m)", "y (m)",
		    "x %d",  iwfs);
	    drawopd("Gcly",(loc_t*)powfs[ipowfs].pts, simu->gradcl->p[iwfs]->p+powfs[ipowfs].pts->nsa, NULL,
		    "WFS Closeloop Gradients (y)","x (m)", "y (m)",
		    "y %d",  iwfs);
	}
    }
}
/**
   Calls wfsgrad_iwfs() to computes WFS gradient in parallel.
*/
void wfsgrad(SIM_T *simu){
    const PARMS_T *parms=simu->parms;
    if(parms->sim.idealfit || parms->sim.evlol) return;
    double tk_start=myclockd();
  
    /* call the task in parallel and wait for them to finish. It may be done in CPU or GPU.*/
    CALL_THREAD(simu->wfs_grad, parms->nwfs, 0);
#if USE_CUDA
    if(parms->gpu.wfs){
	gpu_wfsgrad_save(simu);
    }else
#endif
    wfsgrad_save(simu);
    simu->tk_wfs=myclockd()-tk_start;
}
