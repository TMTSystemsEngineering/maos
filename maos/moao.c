/*
  Copyright 2009, 2010 Lianqi Wang <lianqiw@gmail.com> <lianqiw@tmt.org>
  
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
#include "recon_utils.h"
#include "moao.h"
#include "ahst.h"
/**
   \file moao.c
   Routings to setup moao and carry out moao DM fitting.
*/

/**
   cyclic shift the dmats.  */
static void shift_ring(int nap, dmat **ring, dmat *new){
    dmat *keep=ring[nap-1];
    for(int iap=nap-1; iap>=0; iap--){
	if(!iap){
	    ring[iap]=dref(new);
	}else{
	    ring[iap]=ring[iap-1];
	}
    }
    dfree(keep);
}

/**
   Free MOAO_T
 */
void free_recon_moao(RECON_T *recon, const PARMS_T *parms){
    if(!recon->moao) return;
    for(int imoao=0; imoao<parms->nmoao; imoao++){
	if(!recon->moao[imoao].used) continue;
	locfree(recon->moao[imoao].aloc);
	spcellfree(recon->moao[imoao].HA);
	dcellfree(recon->moao[imoao].NW);
	spcellfree(recon->moao[imoao].actslave);
	spfree(recon->moao[imoao].W0);
	dfree(recon->moao[imoao].W1);
    }
    free(recon->moao);
    recon->moao=NULL;
}
/**
   Prepare the propagation H matrix for MOAO and compute the reconstructor. We
   only need a reconstructor for every different MOAO type.  */
void setup_recon_moao(RECON_T *recon, const PARMS_T *parms){
    const int nmoao=parms->nmoao;
    int used[parms->nmoao];
    memset(used,0,sizeof(int)*nmoao);
    
    for(int ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
	if(parms->powfs[ipowfs].moao>-1){
	    int imoao=parms->powfs[ipowfs].moao;
	    if(imoao<parms->nmoao){
		used[imoao]++;
	    }else{
		error("powfs[%d].moao=%d is inused\n", ipowfs, imoao);
	    }
	}
    }
    if(parms->evl.moao>-1){
	int imoao=parms->evl.moao;
	if(imoao<parms->nmoao){
	    used[imoao]++;
	}else{
	    error("evl.moao=%d is inused\n",imoao);
	}
    }
    int nused=0;
    for(int imoao=0; imoao<nmoao; imoao++){
	if(used[imoao]){
	    nused++;
	}
    }
    if(nused==0) return;//nothing need to be done.
    if(recon->moao){
	free_recon_moao(recon, parms);
    }
    recon->moao=calloc(nmoao, sizeof(MOAO_T));
    for(int imoao=0; imoao<nmoao; imoao++){
	if(!used[imoao]) continue;
	recon->moao[imoao].used=1;
	int order=parms->moao[imoao].order;
	if(order==0){
	    if(parms->ndm>0){
		order=parms->dm[0].order;//inherits.
	    }else{
		error("Please specify the order of the moao DM\n");
	    }
	}
	double dxr=parms->aper.d/order;
	map_t *map=create_metapupil_wrap(parms,0,dxr,0,0,0,T_ALOC,0,parms->fit.square);
	recon->moao[imoao].aloc=sqmap2loc(map);
	sqmapfree(map);
	recon->moao[imoao].aimcc=loc_mcc_ptt(recon->moao[imoao].aloc, NULL);
	dinvspd_inplace(recon->moao[imoao].aimcc);
	recon->moao[imoao].HA=spcellnew(1,1);
	recon->moao[imoao].HA->p[0]=mkh(recon->moao[imoao].aloc, recon->ploc, 
					NULL, 0, 0, 1, 
					parms->moao[imoao].cubic,parms->moao[imoao].iac); 
	if(parms->moao[imoao].lrt_ptt){
	    recon->moao[imoao].NW=dcellnew(1,1);
	    long nloc=recon->moao[imoao].aloc->nloc;
	    recon->moao[imoao].NW->p[0]=dnew(nloc,3);
	    PDMAT(recon->moao[imoao].NW->p[0], pNW);
	    double scl=1./nloc;
	    double scl2=scl*2./parms->aper.d;
	    const double *locx=recon->moao[imoao].aloc->locx;
	    const double *locy=recon->moao[imoao].aloc->locy;
	    for(long iloc=0; iloc<nloc; iloc++){
		//We don't want piston/tip/tilt on the mems.
		pNW[0][iloc]=scl;//piston;
		pNW[1][iloc]=scl2*locx[iloc];//tip
		pNW[2][iloc]=scl2*locy[iloc];//tilt
	    }
	}
	recon->moao[imoao].W0=spref(recon->W0);
	recon->moao[imoao].W1=dref(recon->W1);
	if(parms->moao[imoao].actslave){
	    recon->moao[imoao].actslave=act_slaving(&recon->moao[imoao].aloc, 
						    recon->moao[imoao].HA, 
						    recon->moao[imoao].W1,
						    recon->moao[imoao].NW);
	}
	if(parms->save.setup){
	    locwrite(recon->moao[imoao].aloc,"%s/moao%d_aloc",dirsetup,imoao);
	    spcellwrite(recon->moao[imoao].HA, "%s/moao%d_HA",dirsetup,imoao);
	    dcellwrite(recon->moao[imoao].NW, "%s/moao%d_NW",dirsetup,imoao);
	    spcellwrite(recon->moao[imoao].actslave, "%s/moao%d_actslave",dirsetup,imoao);
	}
	if(parms->plot.setup){
	    plotloc("FoV",parms,recon->moao[imoao].aloc,0,"moao_aloc");
	}
    }//imoao
}

/**
   Apply fit right hand side matrix in CG mode without using assembled matrix
   for MOAO. subtract contributions from DMs that are in common path. Be careful
   which time step the dmcommon is. The DM common should use the commands on the
   step that you are going to apply the MOAO command for. That is the integrator
   output after this computation.  */

static void 
moao_FitR(dcell **xout, const RECON_T *recon, const PARMS_T *parms, int imoao, 
	  double thetax, double thetay, double hs, 
	  const dcell *opdr, const dcell *dmcommon, dcell **rhsout, const double alpha){
  
    //loc_t *maloc=recon->moao[imoao].aloc;
    dcell *xp=dcellnew(1,1);
    xp->p[0]=dnew(recon->ploc->nloc,1);
    
    for(int ipsr=0; ipsr<recon->npsr; ipsr++){
	const double ht = parms->atmr.ht[ipsr];
	double scale=1.-ht/hs;
	prop_nongrid(recon->xloc[ipsr], opdr->p[ipsr]->p,
		     recon->ploc, NULL, xp->p[0]->p, 1, 
		     thetax*ht, thetay*ht, scale, 
		     0, 0);
    }
    for(int idm=0; idm<recon->ndm; idm++){
	const double ht = parms->dm[idm].ht;
	double scale=1.-ht/hs;
	prop_nongrid_cubic(recon->aloc[idm], dmcommon->p[idm]->p,
			   recon->ploc, NULL, xp->p[0]->p, -1, 
			   thetax*ht, thetay*ht, scale, 
			   parms->dm[idm].iac, 0, 0);
    }
    if(rhsout){
	*rhsout=dcelldup(xp);
    }
    double wt=1;
    applyW(xp, recon->moao[imoao].W0, recon->moao[imoao].W1, &wt);
    sptcellmulmat(xout, recon->moao[imoao].HA, xp, alpha);
    dcellfree(xp);
}
/**
   Apply fit left hand side matrix in CG mode
   without using assembled matrix. Slow. don't
   use. Assembled matridx is faster because of multiple
   directions.
*/

static void 
moao_FitL(dcell **xout, const void *A, 
	  const dcell *xin, const double alpha){
    const MOAO_T *moao=(const MOAO_T *)A;
    dcell *xp=NULL;
    double wt=1;
    spcellmulmat(&xp, moao->HA, xin, 1.);
    applyW(xp, moao->W0, moao->W1, &wt);
    sptcellmulmat(xout, moao->HA, xp, alpha);
    dcellfree(xp);xp=NULL;
    dcellmm(&xp, moao->NW, xin, "tn", 1);
    dcellmm(xout,moao->NW, xp, "nn", alpha);
    dcellfree(xp);
    spcellmulmat(xout, moao->actslave, xin, alpha);
}
/**
   mao_recon happens after the common DM fitting and its integrator output
   to take into account the delay in DM commands. there is no close loop
   filtering in MOAO DM commands, but there is still a time delay of 2
   cycles.
*/

void moao_recon(SIM_T *simu){
    const PARMS_T *parms=simu->parms;
    const RECON_T *recon=simu->recon;
    dcell *dmcommon=NULL;
    if(1){//Take OL result
	dcellcp(&dmcommon, simu->dmfit_hi);
    }else{//CL result
	if(parms->sim.closeloop){
	    if(parms->sim.fuseint){
		dcellcp(&dmcommon, simu->dmint[0]);
		if(parms->tomo.split==1){
		    remove_dm_ngsmod(simu, dmcommon);
		}
	    }else{
		dcellcp(&dmcommon, simu->dmint_hi[0]);
		//addlow2dm(&dmcommon, simu,simu->Mint_lo[0], 1);
	    }
	}else{
	    dcellcp(&dmcommon, simu->dmerr_hi);
	}
    }
    dcell *rhs=NULL;
    if(simu->moao_wfs){
	PDCELL(simu->moao_wfs, dmwfs);
	for(int iwfs=0; iwfs<parms->nwfs; iwfs++){
	    int ipowfs=parms->wfs[iwfs].powfs;
	    int imoao=parms->powfs[ipowfs].moao;
	    dcell *rhsout=NULL;
	    if(imoao>-1){
		double hs=parms->powfs[ipowfs].hs;
		dcell *dmmoao=dcellnew(1,1);
		if(recon->warm_restart){
		    dmmoao->p[0]=ddup(dmwfs[iwfs][0]);
		}
		dcellzero(rhs);
		moao_FitR(&rhs, recon, parms,  imoao, 
			  parms->wfs[iwfs].thetax, parms->wfs[iwfs].thetay, 
			  hs, simu->opdr, dmcommon, &rhsout, 1);
		pcg(&dmmoao, moao_FitL, &recon->moao[imoao], NULL, NULL, rhs, 
		    recon->warm_restart, parms->fit.maxit);
		/*if(parms->tomo.split){//remove the tip/tilt form MEMS DM
		    double ptt[3]={0,0,0};
		    loc_t *aloc=recon->moao[imoao].aloc;
		    dmat *aimcc=recon->moao[imoao].aimcc;
		    loc_calc_ptt(NULL, ptt, aloc, 0, aimcc, NULL, dmmoao->p[0]->p);
		    loc_remove_ptt(dmmoao->p[0]->p, ptt, aloc);
		    }*/
		if(!isinf(parms->moao[imoao].stroke)){
		    int nclip=dclip(dmmoao->p[0], 
				    -parms->moao[imoao].stroke,
				    parms->moao[imoao].stroke);
		    if(nclip>0){
			info("wfs %d: %d actuators clipped\n", iwfs, nclip);
		    }
		}
		shift_ring(simu->moao_wfs->nx, dmwfs[iwfs], dmmoao->p[0]);
		if(parms->plot.run){
		    drawopd("MOAO WFS RHS", recon->ploc, rhsout->p[0]->p, 
			    "MOAO for WFS","x (m)", "y(m)", "Wfs rhs %d", iwfs);
		    drawopd("MOAO WFS", recon->moao[imoao].aloc, dmmoao->p[0]->p,
			    "MOAO for WFS","x (m)", "y(m)", "Wfs %d", iwfs);
		}
	
		if(parms->save.dm){
		    cellarr_dmat(simu->save->moao_wfs[iwfs], dmmoao->p[0]);
		}
		dcellfree(rhsout);
		dcellfree(dmmoao);
	    }//if imoao
	}//if wfs

    }
    if(simu->moao_evl){
	PDCELL(simu->moao_evl, dmevl);
	int imoao=parms->evl.moao;
	for(int ievl=0; ievl<parms->evl.nevl; ievl++){
	    dcell *dmmoao=dcellnew(1,1);
	    if(recon->warm_restart){
		dmmoao->p[0]=ddup(dmevl[ievl][0]);//warm restart.
	    }
	    dcell *rhsout=NULL;
	    dcellzero(rhs);
	    moao_FitR(&rhs, recon, parms, imoao, 
		      parms->evl.thetax[ievl], parms->evl.thetay[ievl],
		      INFINITY, simu->opdr, dmcommon, &rhsout, 1);
	    
	    pcg(&dmmoao, moao_FitL, &recon->moao[imoao], NULL, NULL, rhs,
		recon->warm_restart, parms->fit.maxit);
	    /*if(parms->tomo.split){//remove the tip/tilt form MEMS DM
		double ptt[3]={0,0,0};
		loc_t *aloc=recon->moao[imoao].aloc;
		dmat *aimcc=recon->moao[imoao].aimcc;
		loc_calc_ptt(NULL, ptt, aloc, 0, aimcc, NULL, dmmoao->p[0]->p);
		loc_remove_ptt(dmmoao->p[0]->p, ptt, aloc);
		}*/
	    if(!isinf(parms->moao[imoao].stroke)){
		int nclip=dclip(dmmoao->p[0],
				-parms->moao[imoao].stroke,
				parms->moao[imoao].stroke);
		if(nclip>0){
		    info("evl %d: %d actuators clipped\n", ievl, nclip);
		}
	    }
	    shift_ring(simu->moao_evl->nx, dmevl[ievl], dmmoao->p[0]);
	    if(parms->plot.run){
		drawopd("MOAO EVL RHS", recon->ploc, rhsout->p[0]->p, 
			"MOAO for WFS","x (m)", "y(m)", "Evl %d", ievl);
		drawopd("MOAO EVL", recon->moao[imoao].aloc, dmevl[ievl][0]->p,
			"MOAO for EVL","x (m)", "y(m)", "Evl %d", ievl);
	    }
	    if(parms->save.dm){
		cellarr_dmat(simu->save->moao_evl[ievl], dmmoao->p[0]);
	    }	 
	    dcellfree(dmmoao);
	    dcellfree(rhsout);
	}//ievl
    }
    dcellfree(dmcommon);
    dcellfree(rhs);
}