/*
  Copyright 2009-2021 Lianqi Wang <lianqiw-at-tmt-dot-org>
  
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

#include "common.h"
#include "setup_surf.h"
#include "recon_utils.h"
#include "setup_powfs.h"
#include "genseotf.h"

/**
   Setup tilted surface (M3) by ray tracing from the tilted surface to WFS and
   Science grid.

   \todo Merge setup_tsurf, setup_surf, and setup_powfs_ncpa. Be careful about
   which NCPA is compensated by WFS offsets. setup_tsurf and setup_surf act on
   surfaces intersect with most WFS and science, while setup_powfs_ncpa act on
   individual surfaces for each WFS. Maybe it is good idea to keep them
   separate.


   2011-09-07:
   Relocate simu->surfwfs to powfs.opdadd In order to perserve surface for different seeds
*/

static void
setup_surf_tilt(const PARMS_T* parms, APER_T* aper, POWFS_T* powfs, RECON_T* recon){
	info2("Setting up tilt surface (M3)\n");
	rmapcell* tsurf=rmapcellnew(parms->ntsurf, 1);
	for(int itsurf=0; itsurf<parms->ntsurf; itsurf++){
		char* fn=parms->tsurf[itsurf];
		info2("Loading tilt surface from %s\n", fn);
		tsurf->p[itsurf]=rmapread("%s", fn);
	}
	for(int ievl=0; ievl<parms->evl.nevl; ievl++){
		for(int itsurf=0; itsurf<parms->ntsurf; itsurf++){
			m3proj(tsurf->p[itsurf], aper->opdadd->p[ievl], aper->locs,
				parms->evl.thetax->p[ievl], parms->evl.thetay->p[ievl], parms->evl.hs->p[ievl]);
		}
	}
	if(parms->sim.ncpa_calib){
		for(int ifit=0; ifit<parms->sim.ncpa_ndir; ifit++){
			for(int itsurf=0; itsurf<parms->ntsurf; itsurf++){
				m3proj(tsurf->p[itsurf], aper->opdfloc->p[ifit], recon->floc,
					parms->sim.ncpa_thetax->p[ifit], parms->sim.ncpa_thetay->p[ifit], parms->sim.ncpa_hs->p[ifit]);
			}
		}
	}

	for(int iwfs=0; iwfs<parms->nwfs&&powfs; iwfs++){
		const int ipowfs=parms->wfs[iwfs].powfs;
		const int wfsind=parms->powfs[ipowfs].wfsind->p[iwfs];
		loc_t* locwfs;
		if(powfs[ipowfs].loc_tel){
			warning("We don't handle this case yet. Think carefully when to apply shift.\n");
			locwfs=powfs[ipowfs].loc_tel->p[wfsind];
		} else{
			locwfs=powfs[ipowfs].loc;
		}
		for(int itsurf=0; itsurf<parms->ntsurf; itsurf++){
			m3proj(tsurf->p[itsurf], powfs[ipowfs].opdadd->p[wfsind], locwfs,
				parms->wfs[iwfs].thetax, parms->wfs[iwfs].thetay, parms->wfs[iwfs].hs);
		}
	}
	cellfree(tsurf);
}

typedef struct{
	const PARMS_T* parms;
	APER_T* aper;
	POWFS_T* powfs;
	RECON_T* recon;
	map_t* surf;
	int isurf;
	int nevl;
	int nwfs;
	int nncpa;
	int* evlcover;
	int* wfscover;
	int* ncpacover;
	int opdxcover;
}SURF_DATA;

static void prop_surf_evl(thread_t* info){
	SURF_DATA* data=(SURF_DATA*)info->data;
	const PARMS_T* parms=data->parms;
	const APER_T* aper=data->aper;
	const map_t* surf=data->surf;
	const real hl=surf->h;
	const int* evlcover=data->evlcover;
	for(int ievl=info->start; ievl<info->end; ievl++){
		if(!evlcover[ievl]){
			continue;
		}
		const real displacex=parms->evl.thetax->p[ievl]*hl;
		const real displacey=parms->evl.thetay->p[ievl]*hl;
		const real scale=1-hl/parms->evl.hs->p[ievl];
		prop_grid_stat(surf, aper->locs->stat, aper->opdadd->p[ievl]->p,
			1, displacex, displacey, scale, 0, 0, 0);
	}
}

static void prop_surf_ncpa(thread_t* info){
	SURF_DATA* data=(SURF_DATA*)info->data;
	const PARMS_T* parms=data->parms;
	const APER_T* aper=data->aper;
	const RECON_T* recon=data->recon;
	const map_t* surf=data->surf;
	const real hl=surf->h;
	const int* ncpacover=data->ncpacover;
	for(int idir=info->start; idir<info->end; idir++){
		if(!ncpacover[idir]) continue;
		const real displacex=parms->sim.ncpa_thetax->p[idir]*hl;
		const real displacey=parms->sim.ncpa_thetay->p[idir]*hl;
		const real scale=1.-hl/parms->sim.ncpa_hs->p[idir];
		prop_grid(surf, recon->floc, aper->opdfloc->p[idir]->p,
			1, displacex, displacey, scale, 0, 0, 0);
	}
}

static void prop_surf_wfs(thread_t* info){
	SURF_DATA* data=(SURF_DATA*)info->data;
	const PARMS_T* parms=data->parms;
	const POWFS_T* powfs=data->powfs;
	const map_t* surf=data->surf;
	const real hl=surf->h;
	const int* wfscover=data->wfscover;
	for(int iwfs=info->start; iwfs<info->end; iwfs++){
		if(!wfscover[iwfs]){
			continue;
		}
		const int ipowfs=parms->wfs[iwfs].powfs;
		const int wfsind=parms->powfs[ipowfs].wfsind->p[iwfs];
		const real hs=parms->wfs[iwfs].hs;
		const real hc=parms->wfs[iwfs].hc;
		const real scale=1.-(hl-hc)/hs;
		const real displacex=parms->wfs[iwfs].thetax*hl;
		const real displacey=parms->wfs[iwfs].thetay*hl;

		loc_t* locwfs;
		if(powfs[ipowfs].loc_tel){
			locwfs=powfs[ipowfs].loc_tel->p[wfsind];
		} else{
			locwfs=powfs[ipowfs].loc;
		}
		prop_grid(surf, locwfs, powfs[ipowfs].opdadd->p[wfsind]->p,
			1, displacex, displacey, scale, 1., 0, 0);
	}
}

/**
   Setup surface perpendicular to the beam by ray tracing from the surface to
   WFS and Science grid
*/
static void
setup_surf_perp(const PARMS_T* parms, APER_T* aper, POWFS_T* powfs, RECON_T* recon){
	info2("Setting up surface OPD (M1/M2/M3)\n");
	if(fabs(parms->misreg.pupil->p[0])>EPS||fabs(parms->misreg.pupil->p[1])>EPS){
		warning("Please adjust telescope surface ox, oy to account for misregistration. Not doing "
			"in maos because some surfaces may belong to instrument.\n");
	}
	const int nevl=parms->evl.nevl;
	const int nwfs=parms->nwfs;
	const int nncpa=parms->sim.ncpa_ndir;
	int* evlcover=mymalloc(nevl, int);
	int* wfscover=mymalloc(nwfs, int);
	int* ncpacover=mymalloc(nncpa, int);
	int opdxcover;
	SURF_DATA sdata={parms, aper, powfs, recon, NULL, 0, nevl, nwfs, nncpa, evlcover, wfscover, ncpacover, 0};
	const int nthread=NTHREAD;
	thread_t  tdata_wfs[nthread], tdata_evl[nthread], tdata_ncpa[nthread];
	thread_prep(tdata_evl, 0, nevl, nthread, prop_surf_evl, &sdata);
	thread_prep(tdata_wfs, 0, nwfs, nthread, prop_surf_wfs, &sdata);
	if(nncpa){
		thread_prep(tdata_ncpa, 0, nncpa, nthread, prop_surf_ncpa, &sdata);
	}

	for(int isurf=0; isurf<parms->nsurf; isurf++){
		char* fn=parms->surf[isurf];
		if(!fn) continue;
		map_t *surf=0;
		if(zfexist("%s",fn)){
			info2("Loading surface OPD from %s\n", fn);
			surf=mapread("%s", fn);
			if(surf->ny<2||surf->nx<2){
				error("%s: size is %ldx%ld, we expect a 2d map\n", fn, surf->nx, surf->ny);
			}
		}else{
			info2("Generating surface OPD from %s\n", fn);
			surf=genscreen_str(fn);
		}
		//writebin((dmat*)surf, "surf_%d", isurf);
		const char* strname=search_header(surf->header, "SURFNAME");
		const char* strevl=search_header(surf->header, "SURFEVL");
		const char* strwfs=search_header(surf->header, "SURFWFS");
		const char* stropdx=search_header(surf->header, "SURFOPDX");
		if(strname&&(!strcmp(strname, "M1"))){
			surf->ox+=parms->misreg.pupil->p[0];
			surf->oy+=parms->misreg.pupil->p[1];
		}
		//pupil rotation. rotate the surface directly
		if(strname&&(!strcmp(strname, "M1")||!strcmp(strname, "M2"))){
			if(fabs(parms->aper.rotdeg)>1.e-10){
				dmat* B=ddup((dmat*)surf);
				dzero((dmat*)surf);
				dembed((dmat*)surf, B, parms->aper.rotdeg/180.*M_PI);
				dfree(B);
			}
		}
		int evlct=0;
		if(!strevl){
			warning("%s does not contain SURFEVL. Assume it covers all science.\n", fn);
			for(int ievl=0; ievl<nevl; ievl++){
				evlcover[ievl]=1;
			}
			evlct=nevl;
		} else{
			readstr_intarr_relax(&evlcover, nevl, strevl);
			for(int ievl=0; ievl<nevl; ievl++){
				evlct+=evlcover[ievl]?1:0;
			}
			if(evlct>0){
				info2("%s covers evl", fn);
				for(int ievl=0; ievl<nevl; ievl++){
					if(evlcover[ievl]){
						info2(" %d", ievl);
					}
				}
				info2("\n");
			}
		}
		if(evlct==0){
			for(int idir=0; idir<nncpa; idir++){
				ncpacover[idir]=0;
			}
		} else if(evlct==nevl){
			for(int idir=0; idir<nncpa; idir++){
				ncpacover[idir]=1;
			}
		} else{
			error("Not handled\n");
		}
		if(!strwfs){
			warning("%s does not contain SURFWFS, Assume it covers all WFS.\n", fn);
			for(int iwfs=0;iwfs<nwfs; iwfs++){
				wfscover[iwfs]=1;
			}
		} else{
			int* wfscover2=0;
			int ncover=readstr_intarr(&wfscover2, 0, strwfs);
			for(int i=0; i<MIN(ncover, nwfs); i++){
				wfscover[i]=wfscover2[i];
			}
			if(ncover!=nwfs){
				int val;
				if(ncover==0){
					warning("wfscover has zero length. Assume all 0.\n");
					val=0;
				} else{
					//warning("SURFWFS has length of %d, expect %d. Will replicate ", ncover, nwfs);
					if(parms->sim.skysim){
						val=wfscover2[ncover-1];
						info2("last wfs: %d\n", val);
					} else{
						val=wfscover2[0];
						for(int i=1; i<ncover; i++){
							if(val>wfscover2[i]) val=wfscover2[i];
						}
						info2("lowest value: %d\n", val);
					}
				}
				for(int i=ncover; i<nwfs; i++){
					wfscover[i]=val;
				}
			}
			free(wfscover2);
			ncover=0;
			for(int i=0; i<nwfs; i++){
				ncover+=wfscover[i]?1:0;
			}
			if(ncover){
				info2("%s covers WFS", fn);
				for(int i=0; i<nwfs; i++){
					if(wfscover[i]){
						info2(" %d", i);
					}
				}
				info2("\n");
			}
		}
		if(!stropdx){
			opdxcover=1;
		} else{
			opdxcover=(int)readstr_num(stropdx, NULL);
		}
		sdata.evlcover=evlcover;
		sdata.surf=surf;
		sdata.opdxcover=opdxcover;
		sdata.isurf=isurf;
		CALL_THREAD(tdata_evl, 0);
		if(powfs){
			CALL_THREAD(tdata_wfs, 0);
		}
		if(parms->sim.ncpa_calib){
			CALL_THREAD(tdata_ncpa, 0);
		}
		//writebin(powfs[0].opdadd->p[0], "wfs0_%d_%s", isurf,  fn);
		mapfree(surf);
	}
	free(evlcover);
	free(wfscover);
	free(ncpacover);
}

/** We trace rays from Science focal plan OPD to ploc along evaluation
	directions (type=1) or on axis only (type=2).*/
static void FitR_NCPA(dcell** xout, RECON_T* recon, APER_T* aper){
	const PARMS_T* parms=global->parms;
	dcell* xp=NULL;
	if(aper->opdfloc){
		xp=dcelldup(aper->opdfloc);
	} else{
		error("opdfloc is not available\n");
		//the following is wrong for two reasons: 1) ncpa_ndir may be different then evl.nevl, 2) ray tracing from locs to floc is not good because of edge effect.
		xp=dcellnew(parms->sim.ncpa_ndir, 1);
		for(int ievl=0; ievl<parms->sim.ncpa_ndir; ievl++){
			xp->p[ievl]=dnew(recon->floc->nloc, 1);
			prop_nongrid(aper->locs, aper->opdadd->p[ievl]->p,
				recon->floc, xp->p[ievl]->p, 1, 0, 0, 1, 0, 0);
		}
	}
	applyW(xp, recon->W0, recon->W1, parms->sim.ncpa_wt->p);
	dcellmm(xout, recon->HA_ncpa, xp, "tn", 1);
	dcellfree(xp);
}
void FitL_NCPA(dcell** xout, const void* A,
	const dcell* xin, const real alpha){
	const RECON_T* recon=(const RECON_T*)A;
	const PARMS_T* parms=global->parms;
	dcell* xp=NULL;
	dcellmm(&xp, recon->HA_ncpa, xin, "nn", 1.);
	applyW(xp, recon->W0, recon->W1, parms->sim.ncpa_wt->p);
	dcellmm(xout, recon->HA_ncpa, xp, "tn", alpha);
	dcellfree(xp);xp=NULL;
	/*dcellmm(&xp,recon->fitNW, xin, "tn", 1);
	dcellmm(xout,recon->fitNW, xp, "nn", alpha);
	dcellfree(xp);
	if(recon->actslave){
	dcellmm(xout, recon->actslave, xin, "nn", 1);
	}*/
}
static void setup_recon_HAncpa(RECON_T* recon, const PARMS_T* parms){
	const int nevl=parms->sim.ncpa_ndir;
	const int ndm=parms->ndm;
	recon->HA_ncpa=dspcellnew(nevl, ndm);
	dspcell* HA=recon->HA_ncpa/*PDSPCELL*/;
	info2("Generating HA ");TIC;tic;
	for(int ievl=0; ievl<nevl; ievl++){
		real hs=parms->sim.ncpa_hs->p[ievl];
		for(int idm=0; idm<ndm; idm++){
			if(parms->sim.ncpa_calib==2&&idm>0){
				continue;
			}
			const real ht=parms->dm[idm].ht;
			const real scale=1.-ht/hs;
			real displace[2];
			displace[0]=parms->sim.ncpa_thetax->p[ievl]*ht;
			displace[1]=parms->sim.ncpa_thetay->p[ievl]*ht;
			P(HA, ievl, idm)=mkh(recon->aloc->p[idm], recon->floc,
				displace[0], displace[1], scale);
		}
	}
	if(recon->actinterp){
		info2("Replacing HA by HA*actinterp");
		dspcell* HA2=0;
		dcellmm(&HA2, recon->HA_ncpa, recon->actinterp, "nn", 1);
		dspcellfree(recon->HA_ncpa);
		recon->HA_ncpa=HA2;
	}
	toc(" ");
	if(parms->save.setup){
		writebin(recon->HA_ncpa, "HA_ncpa");
	}
}
void lenslet_saspherical(const PARMS_T* parms, POWFS_T* powfs){
	for(int ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
	//Spherical aberration
		if(parms->powfs[ipowfs].saspherical!=0){
			if(fabs(parms->powfs[ipowfs].saspherical)<1){
				error("powfs%d: saspherical=%g should be in nm.\n",
					ipowfs, parms->powfs[ipowfs].saspherical);
			}
			real err=parms->powfs[ipowfs].saspherical*1e-9;

			/*
			  The OPD is
			  Phi=f(r)-a*r^2-b;
			  which r^2=x^2+y^2.
			  f(r) is r^4 for spherical aberration, or custom formula based on measurement (2013-05-16)

			  To simulate focusing the lenslet, we optimize a and b to minimum
			  \Int phi^2. For circular lenslet, the end result will be the same as Z11.

			  let
			  R2=\Int r^2
			  R4=\Int r^4
			  Rf=\Int f(r)
			  Rf2=\Int f(r)*r^2.

			  We have
			  #b=(R4Rf-R2R6)/(R4-R2R2);
			  b=(R2*Rf2-R4*Rf)/(R2*R2-R4*N)
			  a=(R4-b*N)/R2;

			*/
			const int nx=powfs[ipowfs].pts->nx;
			//Can be upgraded to actual amplitude for fill factor, but need to use a full subaperture.
			dmat* ampw=dnew(nx, nx);
			for(int ix=0; ix<nx*nx; ix++){
				ampw->p[ix]=1;
			}
			dnormalize_sumabs(ampw->p, ampw->nx*ampw->ny, 1);

			real nx2=(nx-1)*0.5;
			real fill1d=sqrt(parms->powfs[ipowfs].safill2d);
			//normalize x,y from -1 to 1 in clear aperture
			real Rx2=pow(fill1d*nx/2, -2);
			dmat* opdi=dnew(nx, nx);
			real R2=0, R4=0, RfR2=0, Rf=0, RN=0;
			for(int iy=0; iy<nx; iy++){
				for(int ix=0; ix<nx; ix++){
#define MEASURED_LENSLET 0
					real rr=((iy-nx2)*(iy-nx2)+(ix-nx2)*(ix-nx2))*Rx2;
#if MEASURED_LENSLET
					real xx=(iy-nx2)*(iy-nx2)*Rx2;
					real yy=(ix-nx2)*(ix-nx2)*Rx2;
					P(opdi, ix, iy)=-50.8+49.2*yy+316.6*xx+77.0*yy*yy-309*yy*xx-260.4*xx*xx;
#else
					P(opdi, ix, iy)=rr*rr;
#endif
					real amp=P(ampw, ix, iy);
					R2+=rr*amp;
					R4+=rr*rr*amp;
					Rf+=P(opdi, ix, iy)*amp;
					RfR2+=P(opdi, ix, iy)*rr*amp;
					RN+=amp;
				}
			}
			//real b=(R2*Rf2-R4*R4)/(R2*R2-R4);
			//real a=(R4-b)/R2;
			real b=(R2*RfR2-R4*Rf)/(R2*R2-R4*RN);//fixed in 8/27/2019
			real a=(Rf-b*RN)/R2;
			real var=0;
			for(int iy=0; iy<nx; iy++){
				for(int ix=0; ix<nx; ix++){
					real rr=((iy-nx2)*(iy-nx2)+(ix-nx2)*(ix-nx2))*Rx2;
					P(opdi, ix, iy)-=a*rr+b;
					var+=P(opdi, ix, iy)*P(opdi, ix, iy)*P(ampw, ix, iy);
				}
			}
			dfree(ampw);
			dscale(opdi, err/sqrt(var));
			if(!powfs[ipowfs].opdadd){
				powfs[ipowfs].opdadd=dcellnew(parms->powfs[ipowfs].nwfs, 1);
			}
			for(int jwfs=0; jwfs<parms->powfs[ipowfs].nwfs; jwfs++){
				if(!powfs[ipowfs].opdadd->p[jwfs]){
					powfs[ipowfs].opdadd->p[jwfs]=dnew(powfs[ipowfs].amp->nx, 1);
				}
				for(int isa=0; isa<powfs[ipowfs].pts->nsa; isa++){
					for(int i=0; i<nx*nx; i++){
						powfs[ipowfs].opdadd->p[jwfs]->p[nx*nx*isa+i]+=opdi->p[i];
					}
				}
			}
			dfree(opdi);
		}
	}
}
void lenslet_safocuspv(const PARMS_T* parms, POWFS_T* powfs){
	//defocus specified as P/V
	for(int ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
		if(parms->powfs[ipowfs].safocuspv!=0){
			if(fabs(parms->powfs[ipowfs].safocuspv)<1){
				error("powfs%d: safocuspv should be in nm.\n", ipowfs);
			}
			real pv=parms->powfs[ipowfs].safocuspv*1e-9;
			info2("powfs %d: Put in focus p/v value of %g to subaperture\n", ipowfs, pv*1e9);
			if(!powfs[ipowfs].opdadd){
				powfs[ipowfs].opdadd=dcellnew(parms->powfs[ipowfs].nwfs, 1);
			}
			const int nx=powfs[ipowfs].pts->nx;
			const int nxsa=nx*nx;
			for(int jwfs=0; jwfs<parms->powfs[ipowfs].nwfs; jwfs++){
				if(!powfs[ipowfs].opdadd->p[jwfs]){
					powfs[ipowfs].opdadd->p[jwfs]=dnew(powfs[ipowfs].amp->nx, 1);
				}
				real nx2=(nx-1)*0.5;
				real Rx2=pow(nx2, -2);//do not use fill2d here as defocus is caused by misregistration.
				real rmax2=2*nx2*nx2*Rx2;
				real scale=pv/rmax2;
				for(int isa=0; isa<powfs[ipowfs].pts->nsa; isa++){
					for(int iy=0; iy<nx; iy++){
						for(int ix=0; ix<nx; ix++){
							real rr=((iy-nx2)*(iy-nx2)+(ix-nx2)*(ix-nx2))*Rx2;
							powfs[ipowfs].opdadd->p[jwfs]->p[isa*nxsa+ix+iy*nx]+=rr*scale;
						}
					}
				}
			}
		}
	}
}
/**
   Setup common and non common OPDs from surf and tsurf
 */
void setup_surf(const PARMS_T* parms, APER_T* aper, POWFS_T* powfs, RECON_T* recon){
	if(parms->load.ncpa){
		if(parms->nsurf||parms->ntsurf){
			error("Please disable surf and tsurf when load.ncpa is set\n");
		}
		if(zfexist("%s/surfevl.bin", parms->load.ncpa)){
			aper->opdadd=dcellread("%s/surfevl.bin", parms->load.ncpa);
			if(aper->opdadd->nx!=parms->evl.nevl||aper->opdadd->p[0]->nx!=aper->locs->nloc){
				error("surfevl is in wrong format\n");
			}
		} else{
			warning("%s/surfevl.bin does not exist.\n", parms->load.ncpa);
		}
		if(zfexist("%s/surffloc.bin", parms->load.ncpa)){
			aper->opdfloc=dcellread("%s/surffloc.bin", parms->load.ncpa);
			if(aper->opdfloc->nx!=parms->sim.ncpa_ndir||aper->opdfloc->p[0]->nx!=recon->floc->nloc){
				error("opdfloc is in wrong foramt\n");
			}
		} else{
			warning("%s/surffloc.bin does not exist.\n", parms->load.ncpa);
		}
		for(int ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
			if(zfexist("%s/surfpowfs_%d.bin", parms->load.ncpa, ipowfs)){
				powfs[ipowfs].opdadd=dcellread("%s/surfpowfs_%d.bin", parms->load.ncpa, ipowfs);
				if(powfs[ipowfs].opdadd->nx!=parms->powfs[ipowfs].nwfs){
					error("surfpowfs_%d is in wrong format, expect %d, got %ld\n", ipowfs,
						parms->powfs[ipowfs].nwfs, powfs[ipowfs].opdadd->nx);
				} else{
					for(int jwfs=0; jwfs<parms->powfs[ipowfs].nwfs; jwfs++){
						if(powfs[ipowfs].opdadd->p[jwfs]->nx!=powfs[ipowfs].loc->nloc){
							error("surfpowfs_%d cell %d is in wrong format\n", ipowfs, jwfs);
						}
					}
				}
			} else{
				warning("%s/surfpowfs_%d.bin does not exist.\n", parms->load.ncpa, ipowfs);
			}
		}
	} else{
		for(int ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
			if(parms->powfs[ipowfs].ncpa){
				const dmat* ncpa=parms->powfs[ipowfs].ncpa;
				info("ncpa shape is %ldx%ld\n", ncpa->nx, ncpa->ny);
				if(ncpa->nx!=2){
					error("NCPA is in wrong format: %ldx%ld\n", ncpa->nx, ncpa->ny);
				}
				if(!powfs[ipowfs].opdadd){
					powfs[ipowfs].opdadd=dcellnew_same(parms->powfs[ipowfs].nwfs, 1, powfs[ipowfs].loc->nloc, 1);
				}
				rand_t rstat;
				seed_rand(&rstat, 1);
				for(long im=0; im<ncpa->ny; im++){
					const real rms=P(ncpa, 0, im);
					const int mod=(int)P(ncpa, 1, im);
					if(mod>0){
						dmat* zer=zernike(powfs[ipowfs].loc, parms->aper.d, 0, 0, -mod);
						for(int jwfs=0; jwfs<parms->powfs[ipowfs].nwfs; jwfs++){
							real rms2=rms<0?-rms:rms*(2*randu(&rstat)-1);
							dadd(PP(powfs[ipowfs].opdadd, jwfs,0), 1, zer, rms2);
						}
						dfree(zer);
					}else{
						error("power law to be implemented\n");
					}
				}
			}
		}
		if(parms->nsurf||parms->ntsurf){
			for(int ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
				if(!powfs[ipowfs].opdadd){
					powfs[ipowfs].opdadd=dcellnew_same(parms->powfs[ipowfs].nwfs, 1, powfs[ipowfs].loc->nloc, 1);
				}
			}
			if(!aper->opdadd){
				aper->opdadd=dcellnew_same(parms->evl.nevl, 1, aper->locs->nloc, 1);
			}
			if(!aper->opdfloc&&parms->sim.ncpa_calib){
				aper->opdfloc=dcellnew_same(parms->sim.ncpa_ndir, 1, recon->floc->nloc, 1);
			}

			TIC;tic;
			if(parms->ntsurf>0){
				setup_surf_tilt(parms, aper, powfs, recon);
			}
			if(parms->nsurf>0){
				OMPTASK_SINGLE setup_surf_perp(parms, aper, powfs, recon);
			}
			toc("surf prop");
		}
		/**
		   note about idealfit: No need to pass surfaces to DM fitting
		   routine. These are already contained in dm_ncpa which got to add to the
		   DM command. Nothing needs to be done.
		*/
		//Setup lenslet profile
		lenslet_saspherical(parms, powfs);
		lenslet_safocuspv(parms, powfs);
	}

	if(parms->sim.ncpa_calib){//calibrate NCPA
		int any_evl=0;
		if(aper->opdadd){
			for(int i=0; i<parms->evl.nevl; i++){
				if(dmaxabs(aper->opdadd->p[i])>1e-15){
					any_evl=1;
					break;
				}
			}
		}

		if(any_evl){
			info2("calibrating NCPA\n");
			setup_recon_HAncpa(recon, parms);
			dcell* rhs=NULL;
			FitR_NCPA(&rhs, recon, aper);
			int maxit=40;
			pcg(&recon->dm_ncpa, FitL_NCPA, recon, NULL, NULL, rhs, 1, maxit);
			//don't extrapolate dm_ncpa here.
			dcellfree(rhs);
			if(parms->save.setup){
				writebin(recon->dm_ncpa, "dm_ncpa");
			}
			dspcellfree(recon->HA_ncpa);
		}
		for(int ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
			dcellcp(&powfs[ipowfs].opdbias, powfs[ipowfs].opdadd);
			if(any_evl){//apply NCPA DM command.
				for(int jwfs=0; jwfs<parms->powfs[ipowfs].nwfs; jwfs++){
					int iwfs=parms->powfs[ipowfs].wfs->p[jwfs];
					const real hs=parms->wfs[iwfs].hs;
					const real hc=parms->wfs[iwfs].hc;
					real thetax=parms->wfs[iwfs].thetax;
					real thetay=parms->wfs[iwfs].thetay;
					if(!powfs[ipowfs].opdbias){
						powfs[ipowfs].opdbias=dcellnew_same(parms->powfs[ipowfs].nwfs, 1, powfs[ipowfs].loc->nloc, 1);
					}
					
					for(int idm=0; idm<parms->ndm; idm++){
						if(!recon->dm_ncpa->p[idm]||recon->dm_ncpa->p[idm]->nx==0) continue;
						real ht=parms->dm[idm].ht+parms->dm[idm].vmisreg;
						real scale=1.-(ht-hc)/hs;
						real dispx=ht*thetax;
						real dispy=ht*thetay;
						prop_nongrid(recon->aloc->p[idm], recon->dm_ncpa->p[idm]->p,
							powfs[ipowfs].loc, powfs[ipowfs].opdbias->p[jwfs]->p,
							-1, dispx, dispy, scale, 0, 0);
					}
				}
			}
			if(parms->sim.ncpa_ttr&&powfs[ipowfs].opdbias){
			/*remove average tilt from opdbias and same amount from
			  opdadd. Does not need to be very accurate.*/
				dmat* mcc=loc_mcc_ptt(powfs[ipowfs].loc, powfs[ipowfs].amp->p);
				dinvspd_inplace(mcc);
				for(int jwfs=0; jwfs<parms->powfs[ipowfs].nwfs; jwfs++){
					if(powfs[ipowfs].opdbias->p[jwfs]){
						real ptt[3]={0,0,0};
						loc_calc_ptt(NULL, ptt, powfs[ipowfs].loc, 1./mcc->p[0], mcc,
							powfs[ipowfs].amp->p, powfs[ipowfs].opdbias->p[jwfs]->p);
						loc_remove_ptt(powfs[ipowfs].opdbias->p[jwfs]->p, ptt, powfs[ipowfs].loc);
						if(!powfs[ipowfs].opdadd->p[jwfs]){
							powfs[ipowfs].opdadd->p[jwfs]=dnew(powfs[ipowfs].loc->nloc, 1);
						}
						loc_remove_ptt(powfs[ipowfs].opdadd->p[jwfs]->p, ptt, powfs[ipowfs].loc);
					}
				}
				dfree(mcc);
			}
			if(parms->save.setup){
				writebin(powfs[ipowfs].opdbias, "powfs%d_opdbias", ipowfs);
			}
		}//for ipowfs
	}
	if(parms->save.setup||parms->save.ncpa){
		writebin(aper->opdadd, "surfevl.bin");
		writebin(aper->opdfloc, "surffloc.bin");
		for(int ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
			writebin(powfs[ipowfs].opdadd, "powfs%d_surf.bin", ipowfs);
		}
	}
	if(parms->sim.ncpa_calib&&parms->dbg.ncpa_rmsci&&recon->dm_ncpa&&aper->opdadd){
	//Don't include uncorrectable WFE in science evaluation
		dcellzero(aper->opdadd);
		for(int ievl=0; ievl<parms->evl.nevl; ievl++){
			for(int idm=0; idm<parms->ndm; idm++){
				const real hl=parms->dm[idm].ht;
				const real dispx=parms->evl.thetax->p[ievl]*hl;
				const real dispy=parms->evl.thetay->p[ievl]*hl;
				const real scale=1-hl/parms->evl.hs->p[ievl];
				prop_nongrid(recon->aloc->p[idm], recon->dm_ncpa->p[idm]->p,
					aper->locs, aper->opdadd->p[ievl]->p,
					1, dispx, dispy, scale, 0, 0);
			}
		}
		writebin(aper->opdadd, "surfevl_correctable.bin");
	}
	dcellfree(aper->opdfloc);
	dcellfree(recon->dm_ncpa);
}
