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
#include "sim.h"
/**
   \file maos/cachedm.c Contains routines that prepare and carry out DM shape
   caching on fine sampled grid to speed up ray tracing.  


   Because the DM has coarse sampling, and cubic influence functions, directly
   interpolate from the DM to the WFS and science OPD takes a lot of
   computation. We can speed up this process by first propagating the DM
   commands to a square grid that matches the sampling of the WFS or science
   OPD, and then propagate from the square grid to different WFS or science
   directions. For each sampling and each DM, we only need one fine sampled
   square grid that will cover all the different directions in the FoV of the
   WFSs or science. For the ground DM, we need a plane of 1/64m sampling that
   matches the WFS and science OPD. For the upper DM, we need to two planes, one
   at 1/64m for the LGS WFS and science OPD, and another one at 1/64 m
   *(1-11.2/90) for the LGS due to cone effect. The square grid are on axis.
 */
/**
   Prepares the DM caching structs.
*/
void prep_cachedm(SIM_T *simu){
    const PARMS_T *parms=simu->parms;
    if(!simu->cachedm){
	simu->cachedm=calloc(parms->ndm, sizeof(map_t*));
	for(int idm=0; idm<parms->ndm; idm++){
	    simu->cachedm[idm]=calloc(parms->dm[idm].ncache, sizeof(map_t));
	    for(int iscale=0; iscale<parms->dm[idm].ncache; iscale++){
		long nxout, nyout;
		double oxout, oyout;
		double dx=parms->dm[idm].dxcache[iscale];
		create_metapupil
		    (parms, parms->dm[idm].ht, dx,
		     0, &nxout, &nyout, &oxout, &oyout, NULL,
		     2, 0, T_ALOC,0,0);
		simu->cachedm[idm][iscale].ox=oxout;
		simu->cachedm[idm][iscale].oy=oyout;
		simu->cachedm[idm][iscale].nx=nxout;
		simu->cachedm[idm][iscale].ny=nyout;
		simu->cachedm[idm][iscale].dx=dx;
		simu->cachedm[idm][iscale].p=calloc(nxout*nyout,sizeof(double));
	    }
	}
    }
    int count=0;
    for(int idm=0; idm<parms->ndm; idm++){
	for(int iscale=0; iscale<parms->dm[idm].ncache; iscale++){
	    count++;
	}
    }
    simu->cachedm_n=count;
    simu->pcachedm=malloc(sizeof(int)*2*simu->cachedm_n);
    count=0;
    for(int idm=0; idm<parms->ndm; idm++){
	for(int iscale=0; iscale<parms->dm[idm].ncache; iscale++){
	    simu->pcachedm[count][0]=idm;
	    simu->pcachedm[count][1]=iscale;
	    count++;
	}
    }
    //new scheme for ray tracing
    simu->cachedm_prop=calloc(simu->cachedm_n*simu->nthread, sizeof(thread_t));
    simu->cachedm_propdata=calloc(simu->cachedm_n, sizeof(PROPDATA_T));
    thread_t (*cprop)[simu->nthread]=(void*)simu->cachedm_prop;
    PROPDATA_T *cpropdata=simu->cachedm_propdata;
    for(int ic=0; ic<simu->cachedm_n; ic++){
	int idm=simu->pcachedm[ic][0];
	int iscale=simu->pcachedm[ic][1];

	cpropdata[ic].locin=simu->recon->aloc[idm];
	//cpropdata[ic].phiin=simu->dmreal->p[idm]->p;need to do this at execution
	cpropdata[ic].mapout=&simu->cachedm[idm][iscale];
	cpropdata[ic].alpha=1;
	cpropdata[ic].displacex=0;
	cpropdata[ic].displacey=0;
	cpropdata[ic].scale=1;
	cpropdata[ic].cubic=simu->parms->dm[idm].cubic;
	cpropdata[ic].cubic_iac=simu->parms->dm[idm].iac;
#define INTERLACED 0
	thread_prep(cprop[ic], 0, cpropdata[ic].mapout->ny, INTERLACED, 
		    simu->nthread, (void*)&cpropdata[ic]);
    }
}

/**
   Executed by threads. Each thread handle the next job when done.
*/
static void calc_cachedm_ic(SIM_T *simu){
    int ic;
    int icend=simu->cachedm_n*simu->nthread;
    while(LOCK(simu->mutex_cachedm),ic=simu->cachedm_i++,UNLOCK(simu->mutex_cachedm), ic<icend){
	prop(&simu->cachedm_prop[ic]);
    }
}

/**
   Partition the ray tracing by DM/Destination combinations, as well as
   segments in each combination to maximum efficiency.
*/
void calc_cachedm(SIM_T *simu){
    if(simu->parms->sim.cachedm && simu->dmreal){
	simu->cachedm_i=0;
	if(simu->parms->sim.cachedm){
	    PROPDATA_T *cpropdata=simu->cachedm_propdata;
	    for(int ic=0; ic<simu->cachedm_n; ic++){
		int idm=simu->pcachedm[ic][0];
		int iscale=simu->pcachedm[ic][1];
		cpropdata[ic].phiin=simu->dmreal->p[idm]->p;
		memset(simu->cachedm[idm][iscale].p,0, 
		       sizeof(double) 
		       *simu->cachedm[idm][iscale].nx 
		       *simu->cachedm[idm][iscale].ny);
	    }
	    CALL(calc_cachedm_ic,simu,simu->nthread);
	}
    }
}