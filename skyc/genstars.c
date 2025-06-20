/*
  Copyright 2009-2025 Lianqi Wang <lianqiw-at-tmt-dot-org>
  
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
#include "skyc.h"
#include "genstars.h"
#include "types.h"
#include "photon.h"
/**
   \file genstars.c Generate random stars from besancon star counting model. The
   data file contains J and H magnitudes of all stars within 5 square degree
   FoV. The location of the stars are assumed to be randomly
   distributed. Therefore, the possible number of stars within any sub-field is
   a poisson distribution with mean equal to the average density multiplied with
   area.
 */

/**
   Generate stars for nsky star fields from star catalog.

   \return a cell array of nskyx1, each cell contains (2+nwvl) x nstar array of
location, and magnitudes.  */
dcell* genstars(long nsky,         /**<number of star fields wanted*/
	real lat,        /**<galactic latitude.*/
	real lon,        /**<galactic longitude*/
	real catscl,     /**<Scale the catlog star count.*/
	real fov,        /**<diameter of the patrol field of view in arcsec.*/
	int nwvl,          /**<number of wavelength*/
	real* wvls,      /**<wavelength vector*/
	real maglimit,	/**<If set, remove stars dimmer that this in the first wavelength. */
	rand_t* rstat /**<random stream*/
){
	char fn[80];   //file containing list of stars over the entire fov.
	real cat_fov=0;//Catalog FoV in degree squared
	int Jind=-1;   //Index of J band
	const real wvl_cat[3]={1.25e-6, 1.65e-6, 2.2e-6};
	if(nwvl<=3){
		for(int iwvl=0; iwvl<nwvl; iwvl++){
			if(fabs(wvls[iwvl]-wvl_cat[iwvl])>1e-7){
				error("wvl[%d]=%g does not agree with catalog\n", iwvl, wvls[iwvl]);
			}
		}
	}
	/*if(nwvl==2){
		snprintf(fn, 80, "besancon/JH_5sqdeg_lat%g_lon%g_besancon.bin", lat, lon);
		cat_fov=5.0;//5 arc-degree squared. 
		Jind=0;
	} else */
	if(nwvl<=3){
		snprintf(fn, 80, "besancon/JHK_5sqdeg_lat%g_lon%g_besancon.bin", lat, lon);
		cat_fov=5.0;/*5 arc-degree squared. */
		Jind=0;
	} else{
		Jind=-1;
		error("We only have stars for J+H and J+H+K band. Please fill this part\n");
	}
	info("Loading star catalogue from %s\n", fn);
	dmat* catalog=dread("%s", fn);
	if(catalog->ny<nwvl){
		error("Catalogue has not have enough wavelength\n");
	}
	long ntot=catalog->nx;
	long nsky0=0;
	dcell* res=dcellnew(nsky, 1);
	real fov22=pow(fov/2*AS2RAD, 2);


	real navg0=M_PI*pow(fov/2./3600., 2)/cat_fov*ntot;
	if(catscl>0){//regular sky coverage sim
		real navg=navg0*catscl;
		info("Average number of stars in patrol fov: %g, after scaled by %g\n", navg, catscl);
		/*generate nstart && magnitude according to distribution.*/
		for(long isky=0; isky<nsky; isky++){
			long nstar=randp(rstat, navg);
			if(nstar==0) continue;
			P(res,isky)=dnew(nwvl+2, nstar);
			dmat* pres=P(res,isky);
			for(long istar=0; istar<nstar; istar++){
				long ind=round(ntot*randu(rstat));/*randomly draw a star index in the catlog */
				for(int iwvl=0; iwvl<nwvl; iwvl++){
					P(pres, 2+iwvl, istar)=P(catalog, ind, iwvl);
				}
			}
		}
	} else{
	/*instead of doing draws on nb of stars, we scan all possibilities and
	  assemble the curve in postprocessing.  catscl is negative, with
	  absolute value indicating the max number of J<=19 stars to consider*/
		long nmax=round(-catscl);//maximum number of J<=19 stars. set to 10
		nsky0=nsky/nmax;//number of random instances for each number of J<=19 stars
		if(nsky0*nmax!=nsky){
			error("nsky=%ld, has to be dividable by max # of stars=%ld", nsky, nmax);
		}
		int counti[nmax];//record count in each bin
		memset(counti, 0, sizeof(int)*nmax);
		int count=0;
		while(count<nsky){
			long nstar=randp(rstat, navg0);
			if(nstar==0) continue;
			dmat* tmp=dnew(nwvl+2, nstar);
			dmat* pres=tmp;
			int J19c=0;
			for(long istar=0; istar<nstar; istar++){
				long ind=round((ntot-1)*randu(rstat));
				for(int iwvl=0; iwvl<nwvl; iwvl++){
					P(pres, 2+iwvl, istar)=P(catalog, ind, iwvl);
				}
				if(P(pres, 2+Jind, istar)<=19){
					J19c++;
				}
			}
			//J19c=0 is ok. Do not skip.
			if(J19c<nmax&&counti[J19c]<nsky0){
				int isky=counti[J19c]+(J19c)*nsky0;
				P(res,isky)=dref(tmp);
				count++;
				counti[J19c]++;
			}
			dfree(tmp);
		}
	}
	long ndrop=0;
	/*Fill in the coordinate*/
	for(long isky=0; isky<nsky; isky++){
		if(!P(res,isky)) continue;
		long nstar=P(res,isky)->ny;
		dmat* pres=P(res,isky);
		long jstar=0;
		for(long istar=0; istar<nstar; istar++){
			/*randomly draw the star location. */
			real r=sqrt(fov22*randu(rstat));
			real th=2*M_PI*randu(rstat);
			P(pres, 0, jstar)=r*cos(th);
			P(pres, 1, jstar)=r*sin(th);
			if(jstar!=istar){
				for(int iwvl=0; iwvl<nwvl; iwvl++){
					P(pres, 2+iwvl, jstar)=P(pres, 2+iwvl, istar);
				}
			}
			if(!maglimit || P(pres, 2, istar)<=maglimit){
				jstar++;
			}
		}
		if(jstar<nstar){
			ndrop+=(nstar-jstar);
			if(jstar==0){
				dfree(P(res, isky));
			}else{
				dresize(P(res, isky), -1, jstar);
			}
		}
	}
	if(ndrop){
		info("Removed %ld stars dimmer than the magnitude limit %g at %g\n", ndrop, maglimit, wvl_cat[0]);
	}
	dfree(catalog);
	return res;
}
/**
   The sort function for stars. Sort stars according total flux.
 */
static int sortfun(const real* p1, const real* p2){
	static real Z_J=3.7666e9;
	static real Z_H=2.7206e9;
	real tot1=Z_J*pow(10, -0.4*p1[2])+Z_H*pow(10, -0.4*p1[3]);/*tot flux */
	real tot2=Z_J*pow(10, -0.4*p2[2])+Z_H*pow(10, -0.4*p2[3]);
	return tot1<tot2?1:-1;
}
/**
   Sort the stars with J band magnitude from brightest to dimmest.
*/
void sortstars(dcell* stars){
	for(long isky=0; isky<stars->nx*stars->ny; isky++){
		if(!P(stars,isky)) continue;
		qsort(P(P(stars,isky)), P(stars,isky)->ny, P(stars,isky)->nx*sizeof(real),
			(int(*)(const void*, const void*))sortfun);
	}
}
