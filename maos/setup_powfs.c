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

#include <math.h>
#include <string.h>
#include <alloca.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include "maos.h"
#include <fftw3.h>//after maos.h which defines complex
TIC;
#include "setup_powfs.h"
#include "mtch.h"
#include "genseotf.h"
#define TEST_POWFS 0

/**
   \file maos/setup_powfs.c

   Setting up WFS geometry. like the subaperture location, subaperture grid
   points, physical optics detection transfer function, LGS elongation transfer
   function, etc.

   \todo isolate DTF, ETF, routines, make then generic interface and relocate to the lib folder.

   Do not use sparse interpolation to replace ray tracing for fine sampled
destination grid, especially cubic splines. The interpolation marix takes too
much space.  */

/**
   Free the powfs geometric parameters
*/
static void
free_powfs_geom(POWFS_T *powfs,  const PARMS_T *parms, int ipowfs){
    if(!powfs[ipowfs].pts){
	return;
    }
    ptsfree(powfs[ipowfs].pts);
    locfree(powfs[ipowfs].saloc);
    locfree(powfs[ipowfs].loc);
    free(powfs[ipowfs].amp);
    if(parms->powfs[ipowfs].misreg){
	for(int ilocm=0; ilocm<powfs[ipowfs].nlocm; ilocm++){
	    locfree(powfs[ipowfs].locm[ilocm]);
	    free(powfs[ipowfs].ampm[ilocm]);
	    powfs[ipowfs].ampm[ilocm]=NULL;
	}
	free(powfs[ipowfs].locm);powfs[ipowfs].locm=NULL;
	free(powfs[ipowfs].ampm);powfs[ipowfs].ampm=NULL;
    }
}
/**
   setting up subaperture geometry.
   
   - powfs->pts: the location of the lower left grid coordinate in this subaperture >=saloc.
   
   - powfs->saloc: the lower left corner of the subaperture
   
   - powfs->loc: the x,y coordinates of all the points, ordered acoording to
   subaperture.
   
*/
static void 
setup_powfs_geom(POWFS_T *powfs, const PARMS_T *parms, 
		APER_T *aper, int ipowfs){
    free_powfs_geom(powfs, parms, ipowfs);
    double r2max,offset,dxoffset;
    int count;
 
    //order of the system. 60 for TMT
    const int order  = parms->powfs[ipowfs].order;
    //Subaperture lateral length. 0.5 for TMT.
    const double dxsa = parms->aper.d/(double)order;
    //The subaperture area that are considered as full.
    double areafulli;
    if(order>2){
	areafulli=1.;
    }else{
	areafulli=4./M_PI;
    }
    //The threashold for normalized area (by areafulli) to keep subaperture.
    const double thresarea=parms->powfs[ipowfs].saat;
    r2max = (order/2+1)*(order/2+1);
    //Offset of the coordinate of the center most subaperture from the center.
    if(order & 1){//odd
	offset = -0.5;
    }else{
	offset = 0.0;
    }
    //r2max: Maximum distance^2 from the center to keep a subaperture
    r2max -= 2.*(0.5+offset)*(0.5+offset);
    //the lower left *grid* coordinate of the subaperture
    powfs[ipowfs].pts =calloc(1, sizeof(pts_t));
    powfs[ipowfs].pts->origx =malloc(sizeof(double)*order*order);
    powfs[ipowfs].pts->origy =malloc(sizeof(double)*order*order);
    powfs[ipowfs].pts->area  =malloc(sizeof(double)*order*order);
    //The coordinate of the subaperture (lower left coordinate)
    powfs[ipowfs].saloc=calloc(1,sizeof(loc_t));
    powfs[ipowfs].saloc->locx =malloc(sizeof(double)*order*order);
    powfs[ipowfs].saloc->locy =malloc(sizeof(double)*order*order);
    powfs[ipowfs].pts->dsa   =dxsa;
    /*Number of OPD pixels in 1 direction in each
      subaperture. Make it even to do fft.*/
    int nx = 2*(int)round(0.5*dxsa/parms->powfs[ipowfs].dx);
    const double dx=dxsa/nx;//adjust dx.
    powfs[ipowfs].pts->dx = dx;//sampling
    powfs[ipowfs].pts->nx = nx;

    if(fabs(parms->powfs[ipowfs].dx-powfs[ipowfs].pts->dx)>EPS)
	warning("powfs %d: Adjusting dx from %g to %g\n",
		ipowfs,parms->powfs[ipowfs].dx,powfs[ipowfs].pts->dx);
    if(fabs(dxsa - nx * powfs[ipowfs].pts->dx)>EPS)
	warning("nx=%d,dsa=%f,dx=%f for powfs %d not agree\n",
		nx, dxsa, dx, ipowfs);
    int dxonedge=0;
    if(dxonedge){//do we want to put a point on edge.
	dxoffset=0;
    }else{
	dxoffset=dx/2;
    }
    
    const int nxsa=nx*nx;//Total Number of OPD points.
    powfs[ipowfs].amp=calloc(order*order*nxsa,sizeof(double));
    count = 0;
    /*Collect all the subapertures that are within the allowed
      radius*/
    for(int j=-order/2; j<=(order-1)/2; j++){
	for(int i=-order/2; i<=(order-1)/2; i++){
	    /*subaperture center is at 
	      (i+1/2+offset, j+1/2+offset)*dxsa*/
	    if((i*i+j*j+(i+j)*(1+iceil(2.*offset)))
	       <r2max){
		powfs[ipowfs].pts->origx[count]=
		    ((double)i+offset)*dxsa+dxoffset;
		powfs[ipowfs].pts->origy[count]=
		    ((double)j+offset)*dxsa+dxoffset;
		powfs[ipowfs].saloc->locx[count]=
		    ((double)i+offset)*dxsa;
		powfs[ipowfs].saloc->locy[count]=
		    ((double)j+offset)*dxsa;
		count++;
	    }
	}
    }
    /*Calculate the amplitude for each subaperture OPD point by
      ray tracing from the pupil amplitude map.*/
    double *ampcircle=NULL;
    powfs[ipowfs].pts->nsa=count;
    if(aper->ampground){
	prop_grid_pts(aper->ampground, powfs[ipowfs].pts, 
		      powfs[ipowfs].amp, 1,0,0,1,0,0,0,1);
    }else{
	/*don't use aper->amp here because aper->amp is
	  normalized to sum to 1. We want it to max to 1.*/
	ampcircle=calloc(aper->locs->nloc,sizeof(double));
	loccircle(ampcircle,aper->locs,0,0,parms->aper.d*0.5,1);
	if(parms->aper.din>0){
	    loccircle(ampcircle,aper->locs,0,0,parms->aper.din*0.5,-1);
	}
	prop_nongrid_pts(aper->locs,ampcircle,powfs[ipowfs].pts,
			 NULL,powfs[ipowfs].amp,1,0,0,1,0,0,1);
	
    }
    count=0;
    double maxarea=0;
    double areanormfactor=1./(double)(nxsa);
    /*Go over all the subapertures, calculate the normalized
      subaperture illumination area and remove all that are below
      the are threshold*/
    for(int isa=0; isa<powfs[ipowfs].pts->nsa; isa++){
	double area=0.;
	double *amp=powfs[ipowfs].amp+nxsa*isa;
	double *amp0=powfs[ipowfs].amp+nxsa*count;
	for(int k=0; k<nxsa; k++){
	    area+=amp[k]*amp[k];
	}
	area*=areanormfactor;//normalized the area.
	if(area>1+EPS){//sanity check. impossible.
	    error("area %g is larger than full.\n", area);
	}
	if(area>thresarea){//Area is above threshold, keep.
	    powfs[ipowfs].pts->area[count]=area;
	    if(maxarea < area) maxarea=area;
	    //area is already normalized that maxes to 1.
	    if(count!=isa){
		powfs[ipowfs].pts->origx[count]=
		    powfs[ipowfs].pts->origx[isa];
		powfs[ipowfs].pts->origy[count]=
		    powfs[ipowfs].pts->origy[isa];
		powfs[ipowfs].saloc->locx[count]=
		    powfs[ipowfs].saloc->locx[isa];
		powfs[ipowfs].saloc->locy[count]=
		    powfs[ipowfs].saloc->locy[isa];
		memcpy(amp0, amp, sizeof(double)*nxsa);
	    }
	    count++;
	}
    }
    /*area was already normalized against square subaperture.  to
      scale the physical optics i0. After FFT, the PSF sum to 1
      for square subapertures, but sum to PI/4 for circular TT or
      TTF subapertures. maxarea is 1 for square subapertures, and
      PI/4 for TT and TTF WFS due to cut off by pupil. for square
      subapertures, areascale=1, no effect. for TT or TTF WFS,
      areascale is 4/PI, which scales the PSF from sum to PI/4 to
      sum to 1 again. Used in wfsints*/
    powfs[ipowfs].areascale=1./maxarea;
    for(int isa=0; isa<count; isa++){
	/*noramlize area against 1 for square or PI/4 for arc
	  subapertures. The area is not used in physical
	  optics. It is used to scale geometric sanea.*/
	powfs[ipowfs].pts->area[isa]*=areafulli;
    }
    powfs[ipowfs].npts = count*nxsa;
    powfs[ipowfs].pts->nsa=count;
    powfs[ipowfs].pts->origx=realloc(powfs[ipowfs].pts->origx,
				     sizeof(double)*count);
    powfs[ipowfs].pts->origy=realloc(powfs[ipowfs].pts->origy,
				     sizeof(double)*count);
    powfs[ipowfs].pts->area=realloc(powfs[ipowfs].pts->area,
				    sizeof(double)*count);
    powfs[ipowfs].saloc->locx=realloc(powfs[ipowfs].saloc->locx,
				     sizeof(double)*count);
    powfs[ipowfs].saloc->locy=realloc(powfs[ipowfs].saloc->locy,
				     sizeof(double)*count);
    powfs[ipowfs].saloc->nloc=count;
    powfs[ipowfs].saloc->dx=dxsa;
    
    powfs[ipowfs].amp=realloc(powfs[ipowfs].amp, sizeof(double)*count*nxsa);

    powfs[ipowfs].loc=pts2loc(powfs[ipowfs].pts);
    if(parms->plot.setup){
	drawopd("amp", powfs[ipowfs].loc, powfs[ipowfs].amp,
		"WFS Amplitude Map","x (m)","y (m)","powfs %d", ipowfs);
    }
    if(parms->powfs[ipowfs].misreg){
	/*Create misregistered coordinate and amplitude map to do
	  physical optics wavefront sensing. the un-misregistered
	  coordinate and amplitude map is still used to do the
	  reconstructor because the RTC is not aware of the
	  misregistration.
	 */
	warning("Creating misregistration between DM and WFS.\n");
	dcell *misreg=dcellread("%s",parms->powfs[ipowfs].misreg); 
	if(misreg->nx!=2)
	    error("%s is in wrong format\n",parms->powfs[ipowfs].misreg);
	if(misreg->ny==1){
	    powfs[ipowfs].nlocm=1;
	}else if(misreg->ny==parms->powfs[ipowfs].nwfs){
	    powfs[ipowfs].nlocm=parms->powfs[ipowfs].nwfs;
	}else{
	    error("%s is in wrong format\n",parms->powfs[ipowfs].misreg);
	}
	powfs[ipowfs].locm=calloc(powfs[ipowfs].nlocm, sizeof(loc_t*));
	powfs[ipowfs].ampm=calloc(powfs[ipowfs].nlocm, sizeof(double*));
	PDCELL(misreg,pmisreg);
	for(int ilocm=0; ilocm<powfs[ipowfs].nlocm; ilocm++){
	    /*Transforms loc using misregistration
	      information. The do ray tracing to determine the
	      misregistered ampltidue map*/
	    powfs[ipowfs].locm[ilocm]=loctransform(powfs[ipowfs].loc, 
						   pmisreg[ilocm]);
	    powfs[ipowfs].ampm[ilocm]=calloc(powfs[ipowfs].loc->nloc, 
					     sizeof(double));
	    if(aper->ampground){
		prop_grid(aper->ampground, powfs[ipowfs].locm[ilocm], 
			  powfs[ipowfs].ampm[ilocm], 1,0,0,1,0,0,0);
	    }else{
		prop_nongrid(aper->locs,ampcircle,powfs[ipowfs].locm[ilocm],
			     NULL,powfs[ipowfs].ampm[ilocm],1,0,0,1,0,0);
	    }
	    if(parms->save.setup){
		locwrite(powfs[ipowfs].locm[ilocm],"%s/powfs%d_locm%d.bin.gz",
			 dirsetup,ipowfs,ilocm);
		writedbl(powfs[ipowfs].ampm[ilocm],powfs[ipowfs].loc->nloc,1,
			 "%s/powfs%d_ampm%d.bin.gz", dirsetup,ipowfs,ilocm);
	    }
	}
    }
    if(parms->save.setup){
	locwrite((loc_t*)powfs[ipowfs].pts,
		 "%s/powfs%d_pts.bin.gz",dirsetup,ipowfs);
	locwrite(powfs[ipowfs].saloc,
		 "%s/powfs%d_saloc.bin.gz",dirsetup,ipowfs); 
	writedbl((double*)powfs[ipowfs].pts->area,
		 powfs[ipowfs].pts->nsa,1,"%s/powfs%d_area.bin.gz",
		 dirsetup,ipowfs);
	locwrite(powfs[ipowfs].loc,"%s/powfs%d_loc.bin.gz",dirsetup,ipowfs);
	writedbl((double*)powfs[ipowfs].amp,
		 powfs[ipowfs].loc->nloc,1, "%s/powfs%d_amp.bin.gz",
		 dirsetup,ipowfs);
    }
    if(ampcircle){
	free(ampcircle);
    }
}
/**
   Creating geometric wavefront gradient operator GS0 and ZA0 from WFS OPD to
   subaperture grads. Use loc, not locm. locm is only used to do ray
   tracing.  */
static void 
setup_powfs_grad(POWFS_T *powfs, const PARMS_T *parms, 
		 int ipowfs){
    if(parms->powfs[ipowfs].gtype_recon==0
       ||parms->powfs[ipowfs].gtype_sim==0){
	spfree(powfs[ipowfs].GS0);
	//Setting up every gradient tilt (g-ztilt)
	if(parms->load.GS0){
	    powfs[ipowfs].GS0=spread("powfs%d_GS0.bin.gz",ipowfs);
	}else{
	    double displace[2]={0,0};
	    //This mkg takes about 5 seconds.
	    powfs[ipowfs].GS0=mkg(powfs[ipowfs].loc, 
				  powfs[ipowfs].loc,
				  powfs[ipowfs].amp,
				  powfs[ipowfs].saloc,
				  1, 1, displace, 1);
	}
    }
    if(parms->powfs[ipowfs].gtype_recon==1
       ||parms->powfs[ipowfs].gtype_sim==1){
	//setting up zernike best fit (ztilt) inv(M'*W*M). good for NGS.
	if(parms->powfs[ipowfs].order>4) 
	    warning("Ztilt for high order wfs is not good");
	spfree(powfs[ipowfs].ZS0);
	dcellfree(powfs[ipowfs].mcc);
	dcellfree(powfs[ipowfs].imcc);
	powfs[ipowfs].mcc=pts_mcc_ptt(powfs[ipowfs].pts, powfs[ipowfs].amp);
	powfs[ipowfs].imcc=dcellinvspd_each(powfs[ipowfs].mcc);
	double displace[2]={0,0};
	//ZS0 is a large matrix. 
	//only use for generating GA, GX and testing
	powfs[ipowfs].ZS0=mkz(powfs[ipowfs].loc,powfs[ipowfs].amp,
			      (loc_t*)powfs[ipowfs].pts, 1,1,displace);
    }
    if(parms->save.setup){
	dcellwrite(powfs[ipowfs].imcc,"%s/powfs%d_imcc.bin.gz", dirsetup,ipowfs);
	if(powfs[ipowfs].GS0)
	    spwrite(powfs[ipowfs].GS0,"%s/powfs%d_GS0.bin.gz",dirsetup,ipowfs);
	if(powfs[ipowfs].ZS0)
	    spwrite(powfs[ipowfs].ZS0,"%s/powfs%d_ZS0.bin.gz",dirsetup,ipowfs);
    }
}
/**
   Prepare the parameters for physical optics setup.

   \todo Make LGS number of pixels depend on subapertue elongation in radial
   coordinate CCD mode. Make ncompx, ncompy dependent on number of pixels for radial ccd.
   

*/
static void 
setup_powfs_prep_phy(POWFS_T *powfs,const PARMS_T *parms,int ipowfs){
    const double pixtheta=parms->powfs[ipowfs].pixtheta;
    const int nwvl=parms->powfs[ipowfs].nwvl;
    const int pixpsay=parms->powfs[ipowfs].pixpsa;
    const int radpix=parms->powfs[ipowfs].radpix;
    const double dxsa=powfs[ipowfs].pts->dsa;
    const int nsa=powfs[ipowfs].pts->nsa;
    if(parms->powfs[ipowfs].hasllt){
	const int nllt=parms->powfs[ipowfs].llt->n;
	double rsa2, rsa2max=0;
	dcellfree(powfs[ipowfs].srot);
	dcellfree(powfs[ipowfs].srsa);
	dcellfree(powfs[ipowfs].sprint);

	powfs[ipowfs].srot=dcellnew(nllt,1);
	powfs[ipowfs].srsa=dcellnew(nllt,1);
	powfs[ipowfs].srsamax=dnew(nllt,1);
	powfs[ipowfs].sprint=dcellnew(nllt,1);

	for(int illt=0;illt<nllt;illt++){
	    //adjusted llt center because pts->orig is corner
	    double ox2=parms->powfs[ipowfs].llt->ox[illt]-dxsa*0.5;
	    double oy2=parms->powfs[ipowfs].llt->oy[illt]-dxsa*0.5;
	    powfs[ipowfs].srot->p[illt]=dnew(nsa,1);
	    powfs[ipowfs].srsa->p[illt]=dnew(nsa,1);

	    for(int isa=0; isa<nsa; isa++){
		double ddx=(powfs[ipowfs].saloc->locx[isa]-ox2);
		double ddy=(powfs[ipowfs].saloc->locy[isa]-oy2);
		rsa2=pow(ddx,2)+pow(ddy,2);
		powfs[ipowfs].srot->p[illt]->p[isa]=atan2(ddy,ddx);
		powfs[ipowfs].srsa->p[illt]->p[isa]=sqrt(rsa2);
		if(rsa2>rsa2max) rsa2max=rsa2;
	    }
	    powfs[ipowfs].srsamax->p[illt]=sqrt(rsa2max);
	    int pnsa=(int)ceil(sqrt(rsa2max)/dxsa)+1;
	    
	    double prot[pnsa];
	    powfs[ipowfs].sprint->p[illt]=dnew(pnsa,1);
	    double *pp=powfs[ipowfs].sprint->p[illt]->p;
	    for(int ind=0; ind<pnsa; ind++){
		prot[ind]=INFINITY;
		pp[ind]=-1;
	    }
	    double desrot=0;
	    if(fabs(parms->powfs[ipowfs].llt->ox[illt])<dxsa 
	       && fabs(parms->powfs[ipowfs].llt->oy[illt])<dxsa){
		desrot=0;
	    }else{
		double ddx=(0-parms->powfs[ipowfs].llt->ox[illt]);
		double ddy=(0-parms->powfs[ipowfs].llt->oy[illt]);
		desrot=atan2(ddy,ddx);
	    }
	    for(int isa=0; isa<nsa; isa++){
		int ind=(int)round(powfs[ipowfs].srsa->p[illt]->p[isa]/dxsa);
		double irot=fabs(powfs[ipowfs].srot->p[illt]->p[isa]-desrot);
		if(irot<prot[ind]){
		    prot[ind]=irot;
		    pp[ind]=(double)isa;
		}
	    }
	}
	if(parms->save.setup){
	    dcellwrite(powfs[ipowfs].sprint,"%s/powfs%d_sprint", dirsetup,ipowfs);
	}
    }
    int pixpsax=pixpsay;
    if(radpix<0){//determine radpix adaptively
	//use rsa2max to determine radpix
	error("Not emplemented yet\n");
    }
    pixpsax=(radpix)?radpix:pixpsay;
    
    //record # of detector pixels.
    powfs[ipowfs].pixpsax=pixpsax;
    powfs[ipowfs].pixpsay=pixpsay;

    //to avoid aliasing, fft warping. usually 2.
    const double embfac=parms->powfs[ipowfs].embfac;
 
    //size required to form detector image.
    int ncompx, ncompy;
    if(parms->powfs[ipowfs].ncomp){
	ncompx=parms->powfs[ipowfs].ncomp;
	ncompy=parms->powfs[ipowfs].ncomp;
	warning("powfs %d: ncomp is specified "
		"in input file to %dx%d\n",
		ipowfs,ncompx,ncompy);
    }else{
	/*
	  compute required otf size to cover the detector FoV
	  Need to revise this part: when ncomp is less than the
	  size of the full psf, may need padding.  study aliasing
	  extensively with full, cropped psf and detector
	  transfer function.
	 */
	double wvlmin=parms->powfs[ipowfs].wvl[0];
	for(int iwvl=0; iwvl<nwvl; iwvl++){
	    if(wvlmin>parms->powfs[ipowfs].wvl[iwvl])
		wvlmin=parms->powfs[ipowfs].wvl[iwvl];
	}
	double dtheta=wvlmin/(dxsa*embfac);//Min PSF sampling.
	ncompx=4*(int)round(0.25*pixpsax*pixtheta/dtheta);
	ncompy=4*(int)round(0.25*pixpsay*pixtheta/dtheta);
	
	if(!parms->powfs[ipowfs].radrot){
	    //Follow laos method, need square 
	    ncompx=ncompx>ncompy?ncompx:ncompy;
	    ncompy=ncompx;
	}else{
	    /*Found that: Must set ncompx==ncompy for
	      rotationg either psf or otf. reduce aliasing
	      and scattering of image intensities.
	     */
	    ncompx=ncompx>ncompy?ncompx:ncompy;
	    ncompy=ncompx;
	}
	//A few manual optimizations.
	if(ncompx==ncompy){
	    if(ncompx>8 && ncompx<16){
		ncompx=ncompy=16;
	    }else if(ncompx>16 && ncompx<32){
		ncompx=ncompy=32;
	    }else if(ncompx>64 && ncompx<67){
		ncompx=ncompy=64;
	    }else if(ncompx<128 && ncompx>120){
		ncompx=ncompy=128;
	    }
	}
	info2("ipowfs %d: ncompx=%d, ncompy=%d\n", ipowfs,ncompx,ncompy);
    }//ncomp
    powfs[ipowfs].ncompx=ncompx;
    powfs[ipowfs].ncompy=ncompy;

    if(parms->powfs[ipowfs].bkgrndfn){
	if(parms->powfs[ipowfs].bkgrndrm>2 || parms->powfs[ipowfs].bkgrndrm<0){
	    error("parms->powfs[%d].bkgrndrm has illegal value of %g. "
		  "should be between [0,2]",
		  ipowfs, parms->powfs[ipowfs].bkgrndrm);
	}
	char *fn=find_file(parms->powfs[ipowfs].bkgrndfn);
	info2("Loading sky background/rayleigh backscatter from %s\n",fn);
	dcellfree(powfs[ipowfs].bkgrnd);
	powfs[ipowfs].bkgrnd=dcellread("%s",fn);
	free(fn);
	double bkscale = parms->sim.dt*800*parms->powfs[ipowfs].dtrat;
	if(fabs(bkscale-1)>1.e-20){
	    dcellscale(powfs[ipowfs].bkgrnd, bkscale);
	    warning("powfs %d: Scaling bkgrndfn by %g", ipowfs, bkscale);
	}
	if(powfs[ipowfs].bkgrnd->nx!=powfs[ipowfs].pts->nsa){
	    error("powfs %d: number of rows in bkgrndfn must be %ld, but is %ld\n",
		  ipowfs, powfs[ipowfs].pts->nsa, powfs[ipowfs].bkgrnd->nx);
	}
    }
}
/**
   Set up NCPA maps for each WFS. Load NCPA maps and ray trace to grid of loc */
static void
setup_powfs_ncpa(POWFS_T *powfs, const PARMS_T *parms, int ipowfs){
    const double rot=-parms->aper.rotdeg/180.*M_PI;
    int do_rot=(fabs(rot)>1.e-10);
    const char *fn_ncpa = parms->powfs[ipowfs].ncpa;
    if(fn_ncpa){//Always make nwfs NCPA's
	char *fn=find_file(fn_ncpa);
	int nncpa_in;
	map_t **ncpa=sqmaparrread(&nncpa_in, "%s",fn);
	free(fn);
	const int nwfs=parms->powfs[ipowfs].nwfs;
	if(nncpa_in != 1 && nncpa_in != nwfs){
	    error("powfs[%d].ncpa is in wrong format. nncpa_in=%d, nwfs=%d\n",
		  ipowfs,nncpa_in, nwfs);
	}

	int incpa_mul=0;
	if(nncpa_in==nwfs){
	    incpa_mul=1;
	}
	info2("Creating NCPA\n");
	dcellfree(powfs[ipowfs].ncpa);
	powfs[ipowfs].ncpa=dcellnew(nwfs,1);
	
	for(int iwfs=0; iwfs<nwfs; iwfs++){
	    int ilocm=0;
	    if(powfs[ipowfs].locm && powfs[ipowfs].nlocm>1){//misregistration.
		ilocm=parms->powfs[ipowfs].indwfs[iwfs];
	    }
	    int incpa_in=incpa_mul*iwfs;
	    info2("iwfs=%d, incpa_in=%d\n", iwfs, incpa_in);

	    if(!powfs[ipowfs].ncpa->p[iwfs]){
		powfs[ipowfs].ncpa->p[iwfs]=dnew(powfs[ipowfs].npts,1);
	    }
	    loc_t *locwfsin, *locwfs;
	    if(powfs[ipowfs].locm){
		locwfsin=powfs[ipowfs].locm[ilocm];
	    }else{
		locwfsin=powfs[ipowfs].loc;
	    }
	    if(do_rot){
		info2("Rotating telescope pupil\n");
		locwfs=locdup(locwfsin);
		locrot(locwfs,rot);
	    }else{
		locwfs=locwfsin;
	    }
	    double hl=ncpa[incpa_in]->h;
	    double hs=parms->powfs[ipowfs].hs;
	    const double scale=1.-hl/hs;
	    const double displacex=parms->wfs[iwfs].thetax*hl;
	    const double displacey=parms->wfs[iwfs].thetay*hl;
	    if(ncpa[incpa_in]->nx==1 || ncpa[incpa_in]->ny==1){
		error("ncpa is a vector: Invalid format\n");
	    }
	    prop_grid(ncpa[incpa_in], locwfs, powfs[ipowfs].ncpa->p[iwfs]->p,
		      1, displacex, displacey, scale, 1, 0, 0);
	    if(do_rot){
		locfree(locwfs);
	    }
	}//iwfs;
	sqmaparrfree(ncpa,nncpa_in);
	if(parms->powfs[ipowfs].ncpa_method<0 || parms->powfs[ipowfs].ncpa_method>2){
	    error("Invalid ncpa_method=%d\n", parms->powfs[ipowfs].ncpa_method);
	}
	
	if(parms->powfs[ipowfs].ncpa_method==1){
	    info2("Calculating gradient offset due to NCPA\n");
	    dcellfree(powfs[ipowfs].ncpa_grad);
	    powfs[ipowfs].ncpa_grad=dcellnew(nwfs,1);
	    int nsa=powfs[ipowfs].pts->nsa;
	    for(int iwfs=0; iwfs<nwfs; iwfs++){
		double *realamp;
		if(powfs[ipowfs].locm){
		    if(powfs[ipowfs].nlocm>1)
			realamp=powfs[ipowfs].ampm[iwfs];
		    else
			realamp=powfs[ipowfs].ampm[0];
		}else{
		    realamp=powfs[ipowfs].amp;
		}
		powfs[ipowfs].ncpa_grad->p[iwfs]=dnew(nsa*2,1);
		if(parms->powfs[ipowfs].gtype_sim==1){
		    pts_ztilt(powfs[ipowfs].ncpa_grad->p[iwfs]->p, powfs[ipowfs].pts,
			      powfs[ipowfs].imcc, realamp, powfs[ipowfs].ncpa->p[iwfs]->p);
		}else{
		    spmulmat(&powfs[ipowfs].ncpa_grad->p[iwfs],powfs[ipowfs].GS0,
			     powfs[ipowfs].ncpa->p[iwfs],1);
		}
	    }
	    if(parms->save.setup){
		dcellwrite(powfs[ipowfs].ncpa_grad, "%s/powfs%d_ncpa_grad",dirsetup,ipowfs);
	    }
	}
	if(parms->save.setup){
	    dcellwrite(powfs[ipowfs].ncpa, "%s/powfs%d_ncpa",dirsetup,ipowfs);

	}
    }//if(fn_ncpa)
}
/**
   Free the detector transfer function.
 */
static void 
free_powfs_dtf(POWFS_T *powfs, const PARMS_T *parms, int ipowfs){
	if(powfs[ipowfs].dtf){
	    for(int iwvl=0;iwvl<parms->powfs[ipowfs].nwvl;iwvl++){
		ccellfree(powfs[ipowfs].dtf[iwvl].nominal);
		spcellfree(powfs[ipowfs].dtf[iwvl].si);
		cfree(powfs[ipowfs].dtf[iwvl].Ux);
		cfree(powfs[ipowfs].dtf[iwvl].Uy);
	    }
	    free(powfs[ipowfs].dtf);
	    dfree(powfs[ipowfs].dtheta);
	}
}
/**
   Setting up Detector transfer function used to translate PSFs from FFTs to
   detector pixel images. Integration over pixel size and leaking between pixels
   are considered.
   
   We are going to sample the PSF \f$\phi(\theta)\f$ onto detectors. For a detector
   pixel \f$i\f$ at \f$\theta_{i}\f$, oriented along angle \f$\alpha\f$, we have
   \f[
   I(\theta_{i})=\int\textrm{d}\theta\phi(\theta)S(\theta-\theta_{i})=\int\textrm{d}\theta\phi(\theta-\theta_{i})S(\theta)\f]
   where \f$S(\theta)\f$ is the pixel weighting function.  Rewrite
   the equation in Fourier domain, 

   \f{eqnarray*}{ I(\theta_{i}) & = &
   \int\textrm{d}\theta\int\textrm{d} u\hat{\phi}(u)e^{2\pi i(\theta-\theta_{i})u}S(\theta)\\ & =
   & \int\textrm{d} u\hat{\phi}(u)\hat{S}(-u)e^{2\pi i\theta_{i}u}\\ & = &
   \mathcal{F}^{-1}[\hat{\phi}(u)\hat{S}(-u)](\theta_{i})\f} 

   For a polar coordinate CCD with pixel edges alonging , we have

   The detector pixels can be modeled as a square, convolved with a Gaussian to
   model the pixel blurring (charge leaking)
   \f[
   S(\theta)=S^{\prime}(\theta_{ra})=\sqcap(\theta_{r}/\Delta)\sqcap(\theta_{a}/\Delta)*\frac{1}{2\pi
   \theta_b^2}\exp(-\frac{\theta_r^2+\theta_u^2}{2\theta_b^2})
   \f], where \f$R\f$ is the rotation matrix and 
   \f$\theta_{ra}=R^{T}\theta\f$ is the coordinate in the local radial-azimuthal
   coordinate system for a Polar coordinate CCD, \f$\theta_b\f$ is a measure of the blurring of the pixel. For a pixel aligned along \f$x/y\f$ directions, \f$R\f$ is simply identity.

   The Fourier transform of the detector pixel function \f$S(\theta)\f$ is then

   \f{eqnarray*}{ 
   \hat{S}(u) & = & \int\textrm{d}\theta S(\theta)e^{-2\pi i\theta^{T}u}\\ 
   & = & \int\textrm{d}\theta
   S^{\prime}(R^{T}\theta)e^{-2\pi i\theta^{T}u}\\ 
   & = &\int\textrm{d}(R^{T}\theta)S^{\prime}(R^{T}\theta)e^{-2\pi i(R^{T}\theta)^{T}(R^{T}u)}\\
   & = & \hat{S^{\prime}}(R^{T}u)\f}

   The Fourier transform of \f$\hat{S^{\prime}}\f$ is 
   \f[
   \hat{S^{\prime}}(u_{ra})=\Delta^{2}\textrm{sinc}(u_{r}\Delta)\textrm{sinc}(u_{\theta}\Delta)
   \exp[-2\pi^2\theta_b^2(u_r^2+u_a^2)]
   \f]

   Finally, we have \f[
   I(\theta_{i})=\mathcal{F}^{-1}[\hat{\phi}(u)\Delta^{2}\textrm{sinc}(\Delta(u_{x}\cos\theta+u_{y}\sin\theta))\textrm{sinc}(\Delta(-u_{x}\sin\theta+u_{y}\cos\theta))\exp[-2\pi^2\theta_b^2(u_r^2+u_a^2)](\theta_{i})\f]
   the last \f$\theta_{i}\f$ means evaluate the function at \f$\theta_{i}\f$ by
   interpolation. When there is leaking between actuators. We can multiply the
   sinc functions with a Gaussian blurring function.
    */
static void 
setup_powfs_dtf(POWFS_T *powfs,const PARMS_T *parms,int ipowfs){
    tic;
    //wvl independent parameters
    const double dxsa=powfs[ipowfs].pts->dsa;
    const int nsa=powfs[ipowfs].pts->nsa;
    const double pixtheta=parms->powfs[ipowfs].pixtheta;
    const double blur=parms->powfs[ipowfs].pixblur*pixtheta;
    const double e0=exp(-2*M_PI*M_PI*blur*blur);//blurring factors
    const int ncompx=powfs[ipowfs].ncompx;
    const int ncompy=powfs[ipowfs].ncompy;
    const int ncompx2=ncompx>>1;
    const int ncompy2=ncompy>>1;
    const int nwvl=parms->powfs[ipowfs].nwvl;
    const int pixpsax=powfs[ipowfs].pixpsax;
    const int pixpsay=powfs[ipowfs].pixpsay;
    const double embfac=parms->powfs[ipowfs].embfac;
    const double pxo=-(pixpsax/2-0.5+parms->powfs[ipowfs].pixoffx)*pixtheta;
    const double pyo=-(pixpsay/2-0.5+parms->powfs[ipowfs].pixoffy)*pixtheta;
    int ndtf;
    int nllt;
    int multi_dtf=0;
    if(parms->powfs[ipowfs].hasllt&&!parms->powfs[ipowfs].radrot
       &&parms->powfs[ipowfs].radpix){
	/*When we have llt, there is elongation, radial pix but
	  not use rotating psf/otf Need to create nominal/si for
	  each subaperture. DTF is on x/y coordinate, so pixels
	  and pixel coordinates need to rotated to r/a
	  direction. */
	ndtf=nsa;
	nllt=parms->powfs[ipowfs].llt->n;
	multi_dtf=1;
    }else{
	//We only need a single DTF.
	ndtf=1;
	nllt=1;
	multi_dtf=0;
    }
    powfs[ipowfs].dtf=calloc(nwvl,sizeof(DTF_T));
    powfs[ipowfs].dtheta=dnew(nwvl,1);
    for(int iwvl=0; iwvl<nwvl; iwvl++){
	const double wvl=parms->powfs[ipowfs].wvl[iwvl];
	const double dtheta=wvl/(dxsa*embfac);//PSF sampling.
	const double dux=1./(dtheta*ncompx);
	const double duy=1./(dtheta*ncompy);
	const double dux2=dux*dux;
	const double duy2=duy*duy;
	const double pdtheta=pixtheta*pixtheta/(dtheta*dtheta);
	const double duyp=duy*pixtheta;
	const double duxp=dux*pixtheta;
	powfs[ipowfs].dtheta->p[iwvl]=dtheta;
	powfs[ipowfs].dtf[iwvl].nominal=ccellnew(ndtf,nllt);
	powfs[ipowfs].dtf[iwvl].si=spcellnew(ndtf,nllt);
	cmat*(*nominals)[ndtf]=
	    (void*)powfs[ipowfs].dtf[iwvl].nominal->p;
	dsp*(*sis)[ndtf]=(void*)powfs[ipowfs].dtf[iwvl].si->p;
	cmat *nominal=cnew(ncompx,ncompy);
	cfft2plan(nominal,-1);
	cfft2plan(nominal,1);
	PCMAT(nominal,pn);
	loc_t *loc_psf=mksqloc(ncompx,ncompy,dtheta, -ncompx2*dtheta, -ncompy2*dtheta);
	double theta=0;
	double ct=cos(theta);
	double st=sin(theta);
	for(int illt=0; illt<nllt; illt++){
	    for(int isa=0; isa<ndtf; isa++){
		if(multi_dtf){
		    theta=powfs[ipowfs].srot->p[illt]->p[isa];
		    ct=cos(theta);
		    st=sin(theta);
		}
		
		for(int iy=0; iy<ncompy; iy++){
		    int jy=iy-ncompy2;
		    for(int ix=0; ix<ncompx; ix++){
			int jx=ix-ncompx2;
			double ir=ct*jx+st*jy;
			double ia=-st*jx+ct*jy;
			pn[iy][ix]=sinc(ir*duyp)*sinc(ia*duxp)
			    *pow(e0, ir*ir*duy2 + ia*ia*dux2)
			    *pdtheta;
		    }
		}
		/*put peak in corner. pretreat nominal so
		  that we avoid fftshift when forming i0.*/
		
		cfftshift(nominal);
		cfft2(nominal,-1);
		cfftshift(nominal);
		cfft2(nominal,1);
		cscale(nominal,1./(double)(nominal->nx*nominal->ny));
		
		ccp(&nominals[illt][isa], nominal);
		//nominals[illt][isa]=cffttreat(nominal);
		loc_t *loc_ccd=mksqlocrot(pixpsax,pixpsay,
					  pixtheta,pxo,pyo,theta);
		sis[illt][isa]=mkh(loc_psf,loc_ccd,NULL,0,0,1,0,0);
		locfree(loc_ccd);
	    }//isa
	}//illt
	cfree(nominal);
	locfree(loc_psf);
	if(parms->save.setup){
	    ccellwrite(powfs[ipowfs].dtf[iwvl].nominal,
		       "%s/powfs%d_dtf%d_nominal.bin.gz",dirsetup,ipowfs,iwvl);
	    spcellwrite(powfs[ipowfs].dtf[iwvl].si,
			"%s/powfs%d_dtf%d_si.bin.gz",dirsetup,ipowfs,iwvl);
	}
	/*Create an excessive high frequency in nominal so that
	  we don't have to do fftshift later.*/
	powfs[ipowfs].dtf[iwvl].Ux=cnew(ncompx,1);
	powfs[ipowfs].dtf[iwvl].Uy=cnew(ncompy,1);
	dcomplex *Ux=powfs[ipowfs].dtf[iwvl].Ux->p;
	dcomplex *Uy=powfs[ipowfs].dtf[iwvl].Uy->p;

	/*The following is used in genseotf to compute shifted
	  i0.  not used currently*/
	for(int ix=0; ix<ncompx; ix++){
	    int jx=ix<ncompx2?ix:(ix-ncompx);
	    Ux[ix]=-2.*I*M_PI*jx*dux;
	}
	for(int iy=0; iy<ncompy; iy++){
	    int jy=iy<ncompy2?iy:(iy-ncompy);
	    Uy[iy]=-2.*I*M_PI*jy*duy;
	}

    }//iwvl
    toc("dtf");
}
/**
   setup the range to sodium layer as an additional parameter.
*/
static void setup_powfs_focus(POWFS_T *powfs, const PARMS_T *parms, int ipowfs){
    if(!parms->powfs[ipowfs].hasllt || !parms->powfs[ipowfs].llt->fnrange) return;
    char *fnrange=find_file(parms->powfs[ipowfs].llt->fnrange);
    warning("ipowfs %d: loading sodium range from %s\n", ipowfs, fnrange);
    if(powfs[ipowfs].focus) dfree(powfs[ipowfs].focus);
    powfs[ipowfs].focus=dread("%s",fnrange);
    free(fnrange);
    if(powfs[ipowfs].focus->ny!=1 &&powfs[ipowfs].focus->ny!=parms->powfs[ipowfs].nwfs){
	error("fnrange has wrong format. Must be column vectors of 1 or %d columns\n",
	      parms->powfs[ipowfs].nwfs);
    }
    double *p=powfs[ipowfs].focus->p;
    //Convert range to focus.
    double range2focus=pow(parms->aper.d/parms->powfs[ipowfs].hs,2)/(16.*sqrt(3));
    for(int ii=0; ii<powfs[ipowfs].focus->nx*powfs[ipowfs].focus->ny; ii++){
	p[ii]*=range2focus;
    }
}

/**
  Load and smooth out sodium profile. We preserve the sum of the sodium profile,
which represents a scaling factor of the signal level.  */
static void setup_powfs_sodium(POWFS_T *powfs, const PARMS_T *parms, int ipowfs){
    char *lltfn=find_file(parms->powfs[ipowfs].llt->fn);
    dmat *Nain=dread("%s",lltfn);
    if(Nain->ny<2){
	error("The sodium profile input %s is in wrong fromat\n", lltfn);
    }
    free(lltfn);

    if(parms->powfs[0].llt->smooth){//resampling the sodium profile by binning.
	//Make new sampling:
	double rsamax=dmax(powfs[ipowfs].srsamax);
	double dthetamin=dmin(powfs[ipowfs].dtheta);
	const double x0in=Nain->p[0];
	const long nxin=Nain->nx;
	double dxin=(Nain->p[nxin-1]-x0in)/(nxin-1);
	//minimum sampling required.
	double dxnew=pow(parms->powfs[ipowfs].hs,2)/rsamax*dthetamin;
	if(dxnew > dxin * 2){
	    info2("Smoothing sodium profile\n");
	    const long nxnew=ceil((Nain->p[nxin-1]-x0in)/dxnew);
	    loc_t *loc_in=mk1dloc_vec(Nain->p, nxin);
	    loc_t *loc_out=mk1dloc(x0in, dxnew, nxnew);
	    dsp *ht=mkhb(loc_out, loc_in, NULL, 0, 0, 1,0,0);
	    powfs[ipowfs].sodium=dnew(nxnew, Nain->ny);
	    memcpy(powfs[ipowfs].sodium->p, loc_out->locx, sizeof(double)*nxnew);
			     
	    for(long icol=1; icol<Nain->ny; icol++){
		//input profile
		double *pin=Nain->p + nxin * icol;
		//output profile
		double *pout=powfs[ipowfs].sodium->p + nxnew*icol;
		//preserve sum of input profile
		double Nasum=dblsum(pin, nxin);
		spmulvec(pout, ht, pin, 1);
		normalize(pout, nxnew, Nasum);
	    }
	    spfree(ht);
	    locfree(loc_in);
	    locfree(loc_out);
	    dfree(Nain);
	}else{
	    warning("Not smoothing sodium profile\n");
	    powfs[ipowfs].sodium=Nain;
	}
    }else{
	warning("Not smoothing sodium profile\n");
	powfs[ipowfs].sodium=Nain;
    }
    if(parms->save.setup){
	dwrite(powfs[ipowfs].sodium, "%s/powfs%d_sodium",dirsetup,ipowfs);
    }
}
/**
   Compute Elongation Transfer function.
   - mode=0: for preparation.
   - mode=1: for simulation.
*/
void setup_powfs_etf(POWFS_T *powfs, const PARMS_T *parms, int ipowfs, int mode, int istep){
    if(!parms->powfs[ipowfs].hasllt) return;
    const double dxsa=powfs[ipowfs].pts->dsa;
    const int nsa=powfs[ipowfs].pts->nsa;
    const int nllt=parms->powfs[ipowfs].llt->n;
    const int nwvl=parms->powfs[ipowfs].nwvl;
    const int ncompx=powfs[ipowfs].ncompx;
    const int ncompy=powfs[ipowfs].ncompy;
    const int ncompx2=ncompx>>1;
    const int ncompy2=ncompy>>1;
    ETF_T *powfsetf=NULL;
    int colstart, colskip;
    if(mode==0){//preparation.
	powfs[ipowfs].etfprep=calloc(nwvl, sizeof(ETF_T));
	powfsetf=powfs[ipowfs].etfprep;
	colskip=parms->powfs[ipowfs].llt->colprep;
    }else{
	if(parms->powfs[ipowfs].llt->colprep==parms->powfs[ipowfs].llt->colsim
	   && parms->powfs[ipowfs].llt->colsimdtrat==0){
	    if(powfs[ipowfs].etfsim){
		error("powfs %d: etfsim should be empty\n",ipowfs);
	    }
	    if(!powfs[ipowfs].etfprep){
		error("powfs %d: Please call setup_powfs_etf with mode = 0 first\n",ipowfs);
	    }
	    powfs[ipowfs].etfsim=powfs[ipowfs].etfprep;
	    info2("powfs %d: simulation and reconstruction using same etf\n",ipowfs);
	    return;
	}
	if(powfs[ipowfs].etfsim){
	    info2("powfs %d: Free previous etfsim\n",ipowfs);
	    for(int iwvl=0;iwvl<parms->powfs[ipowfs].nwvl;iwvl++){
		ccellfree(powfs[ipowfs].etfsim[iwvl].p1);
		ccellfree(powfs[ipowfs].etfsim[iwvl].p2);
	    }
	    free(powfs[ipowfs].etfsim);
	}
	powfs[ipowfs].etfsim=calloc(nwvl, sizeof(ETF_T));
	powfsetf=powfs[ipowfs].etfsim;
	colskip=parms->powfs[ipowfs].llt->colsim;
    }
    //find maximum elongation. 
    //Then test if # of pixels large enough.
 
    assert(nwvl==1);//sanity check. need to double check the code is nwvl!=1.
    const double embfac=parms->powfs[ipowfs].embfac;
    //setup elongation along radial direction. don't care azimuthal.
    for(int iwvl=0; iwvl<nwvl; iwvl++){
	const double wvl=parms->powfs[ipowfs].wvl[iwvl];
	const double dtheta=wvl/(dxsa*embfac);//PSF sampling.
	/**
	   fixme: should do integrate instead of just interpolating closest point. 
	*/
	cmat *(*petf)[nsa]=NULL;
	const int ndtf=powfs[ipowfs].dtf[iwvl].nominal->nx;
	cmat *(*pnominal)[ndtf]=(void*)powfs[ipowfs].dtf[iwvl].nominal->p;
	int mnominal;//multiply with isa to get index into pnominal.
	if(ndtf==1)
	    mnominal=0;
	else if(ndtf==nsa)
	    mnominal=1;
	else{
	    mnominal=-1;
	    error("Invalid nominal.\n");
	}
	int use1d;

	if(parms->powfs[ipowfs].radrot){
	    if(!parms->powfs[ipowfs].radpix){
		error("radrot can only be used with radpix\n");
	    }
	    /*
	      Use rotating psf/otf method to do radial
	      pixel. ETF is 1D only. Good for off-axis
	      launch, otherwise, ETF and DTF takes a lot of
	      space for 6 LGS in NFIRAOS setup
	      warning("Using rot psf/otf method to do radial
	      pixel detector\n");
	    */
#if ROT_OTF == 1
	    warning("powfs %d: rotate OTF to do radial format detector (non-preferred)\n",ipowfs);
#elif ROT_OTF==0
	    warning("powfs %d: rotate PSF to do radial format detector (preferred)\n",ipowfs);
#else
	    error("Invalid option\n");
#endif
	    powfsetf[iwvl].p1=ccellnew(nsa,nllt);
	    petf=(void*)powfsetf[iwvl].p1->p;
	    use1d=1;
	}else{
	    /*
	      Applied to 2 cases: 
	      1) radial ccd, not rotating PSF or OTF
	      2) Non-radial ccd
	      2010-01-04: Fuse dtf nominal into etf for this case.
	    */
	    if(parms->powfs[ipowfs].radpix){
		info2("powfs %d: 2D ETF for Radial CCD\n",ipowfs);
	    }else{
		info2("powfs %d: Non-Radial CCD\n",ipowfs);
	    }
	    powfsetf[iwvl].p2=ccellnew(nsa,nllt);
	    petf=(void*)powfsetf[iwvl].p2->p;
	    use1d=0;
	}
	const int fuse_etf=(use1d==0);
	cmat *etf=NULL;
	int netf;
	double dusc;
	double dtetf;
	int npad;
	int nover;
	if(use1d){
	    /*
	      2009-12-18:Thinking of padding here also to reduce aliasing. 
	      result: helps little, not much.
	    */
	    npad=2;//zero padding to reduce aliasing?
	    nover=2;
	    //padding by x2.
	    netf=ncompx*nover*npad;
	    dtetf=dtheta/nover;
	}else{
	    /*
	      We padd the array to incrase the sampling of
	      du. Don't shrink dtheta.  in use1d==0 case, du in
	      etf is smaller.  so interpolation will be more
	      accurate.
	    */
	    npad=2;
	    nover=2;//doesn't change a lot. reduce aliasing
	    //padding by x2.
	    netf=ncompx*nover*npad;
	    //make du array bigger so that rotation is covered
	    dtetf=dtheta/nover;
	}
	dusc=(netf*dtetf)/(dtheta*ncompx);
	etf=cnew(netf,1);
	double *thetas=calloc(netf, sizeof(double));
	int netf2=netf>>1;
	//Only interpolating the center part. the rest is padding.
	int etf0=netf2-(int)round(ncompx2*(dtheta/dtetf));
	int etf1=etf0+(int)round(ncompx*(dtheta/dtetf));
	if(etf0<0) error("Invalid configuration\n");
	for(int it=etf0; it<etf1; it++){
	    thetas[it]=(it-netf2)*dtetf;
	}
	cfft2plan(etf, -1);
	if(!powfs[ipowfs].sodium){
	    error("Sodium profile is NULL\n");
	}
	dmat *sodium=powfs[ipowfs].sodium;
       	int nhp=sodium->nx; 

	const double hs=parms->powfs[ipowfs].hs;
	double hpmin=sodium->p[0]/cos(parms->sim.za);
	double dhp1=1./(sodium->p[1]-sodium->p[0]);
	//assume linear spacing. check the assumption valid
	if(fabs(sodium->p[nhp-1]-sodium->p[0]-(nhp-1)/dhp1)>1.e-7){
	    error("llt profile is not evenly spaced:%g\n",
		  fabs(sodium->p[nhp-1]-sodium->p[0]-(nhp-1)/dhp1));
	}
	dhp1=dhp1*cos(parms->sim.za);
	if((sodium->ny-1-colskip)<0){
	    error("Invalid configuration. colprep or colsim is too big\n");
	}
	colstart=colskip+istep%(sodium->ny-1-colskip);
	info2("powfs %d: Na using column %d in %s\n",ipowfs,colstart,
	     mode==0?"preparation":"simulation");
	//points to the effective sodium profile.
	double* pp=sodium->p+nhp*(1+colstart);
	/*if(fabs(pp[0])>1.e-15 || fabs(pp[nhp-1])>1.e-15){
	    warning("Changed sodium profile both ends to zero.\n");
	    pp[0]=0;
	    pp[nhp-1]=0;
	    }*/
	//the sum of pp determines the scaling of the pixel intensity.
	double i0scale=dblsum(pp, nhp);
	if(fabs(i0scale-1)>0.01){
	    warning("powfs %d: siglev is scaled by %g by sodium profile\n", ipowfs, i0scale);
	}
	if(i0scale<0.8 || i0scale>2){
	    error("Check whether this is valid. Relax the restriction if desired.\n");
	}
	for(int illt=0; illt<nllt; illt++){
	    for(int isa=0; isa<nsa; isa++){
		//1d ETF along radius.
		double rsa=powfs[ipowfs].srsa->p[illt]->p[isa];
		double etf2sum=0;
		czero(etf);
		for(int icomp=etf0; icomp<etf1; icomp++){
		    //peak in center
		    const double itheta=thetas[icomp];
		    //non linear mapping.
		    const double ih=hs*rsa/(rsa-itheta*hs);
		    //interpolating to get Na profile strenght.
		    //this is bilinear interpolation. not good. 
		    //need to do averaging
		    const double iih=(ih-hpmin)*dhp1;
		    const int iihf=ifloor(iih);
		    const double iihw=iih-iihf;
		    if(iihf<0 || iihf>nhp-2){
			etf->p[icomp]=0.;
		    }else{
			double tmp=pp[iihf]*(1.-iihw)+pp[iihf+1]*iihw;
			//neglected rsa1 due to renormalization.
			etf->p[icomp]=tmp;
			etf2sum+=tmp;
		    }
		}
		if(fabs(etf2sum)>1.e-20){
		    /*2010-11-09:

		      We used to normalize the etf before fft so that after fft
		      it max to 1. The strength of original profile doesn't
		      matter.
		    
		      Changed: We no longer normalize the etf, so we can model
		      the variation of the intensity and meteor trails.
		      
		    */
		    cscale(etf,i0scale/etf2sum);
		    cfftshift(etf);//put peak in corner;
		    cfft2(etf, -1);
		    if(use1d){
			if(npad==1 && nover==1){
			    ccp(&petf[illt][isa],etf);
			}else{
			    cfftshift(etf);
			    petf[illt][isa]=cnew(ncompx,1);
			    dcomplex *etf1d=petf[illt][isa]->p;
			    for(int icompx=0; icompx<ncompx; icompx++){
				double ir=dusc*(icompx-ncompx2)+netf2;
				int iir=ifloor(ir);
				ir=ir-iir;
				if(iir>=0 && iir<netf-1){
				    etf1d[icompx]=etf->p[iir]*(1.-ir)
					+etf->p[iir+1]*ir;
				}/*else{etf1d[icompx]=0;}*/
			    }
			    cfftshift(petf[illt][isa]);
			}
		    }else{
			//Rotate the ETF.
			//need to put peak in center.
			cfftshift(etf);
			double theta=powfs[ipowfs].srot->p[illt]->p[isa];
			double ct=cos(theta);
			double st=sin(theta);
			petf[illt][isa]=cnew(ncompx,ncompy);
			dcomplex (*etf2d)[ncompx]=(void*)petf[illt][isa]->p;
			for(int icompy=0; icompy<ncompy; icompy++){
			    double iy=(icompy-ncompy2);
			    for(int icompx=0; icompx<ncompx; icompx++){
				double ix=(icompx-ncompx2);
				double ir=(dusc*(ct*ix+st*iy))+netf2;//index in etf
				int iir=ifloor(ir);
				ir=ir-iir;
				if(iir>=0 && iir<netf-1){
				    //bilinear interpolation.
				    etf2d[icompy][icompx]=etf->p[iir]*(1.-ir)
					+etf->p[iir+1]*ir;
				}/*else{etf2d[icompy][icompx]=0;}*/
			    }
			}
			cfftshift(petf[illt][isa]);//peak in corner;
		    }
		}else{
		    warning("Wrong focus!\n");
		    if(use1d){
			petf[illt][isa]=cnew(ncompx,1);
		    }else{
			petf[illt][isa]=cnew(ncompx,ncompy);
		    }
		    cset(petf[illt][isa],1);
		}
		if(fuse_etf){
		    //Fuse dtf into this 2d etf.
		    ccwm(petf[illt][isa], pnominal[illt][isa*mnominal]);
		}
	    }
	}
	if(fuse_etf){
	    powfs[ipowfs].dtf[iwvl].fused=1;
	    if(parms->powfs[ipowfs].llt->colprep==parms->powfs[ipowfs].llt->colsim
	       && parms->powfs[ipowfs].llt->colsimdtrat==0){
		ccellfree(powfs[ipowfs].dtf[iwvl].nominal);
		info2("powfs %d: DTF nominal is fused to ETF and freed\n", ipowfs);
	    }else{
		info2("powfs %d: DTF nominal is fused to ETF but kept\n", ipowfs);
	    }
	}	    
	
	cfree(etf);
	free(thetas);
    }
}

/**
   setting up uplink pts/amp lotf
*/
static void 
setup_powfs_llt(POWFS_T *powfs, const PARMS_T *parms, int ipowfs){

    if(!parms->powfs[ipowfs].hasllt) return;
    const double dxsa=powfs[ipowfs].pts->dsa;
    const int nwvl=parms->powfs[ipowfs].nwvl;
    powfs[ipowfs].lotf=calloc(1, sizeof(LOTF_T));
    pts_t *lpts=powfs[ipowfs].lotf->pts=calloc(1, sizeof(pts_t));
    lpts->nsa=1;
    const double dx = powfs[ipowfs].pts->dx;
    double lltd=parms->powfs[ipowfs].llt->d;
    const int nx = powfs[ipowfs].pts->nx;
    if(lltd>dxsa){
	warning("The LLT has a diameter greater than subaperture diameter. Will be cropped\n");
    }
    //llt is no larger than subaperture.
    lpts->dx=dx;
    lpts->dsa=dxsa;
    lpts->nx=nx;
	
    lpts->origx=calloc(1, sizeof(double));
    lpts->origy=calloc(1, sizeof(double));
    lpts->area=calloc(1, sizeof(double));

    double oy=lpts->origx[0]=-dxsa/2+dx/2;
    double ox=lpts->origy[0]=-dxsa/2+dx/2;
    lpts->area[0]=1;
    //fixme: when LLT is larger than dsa, need to change this.
    powfs[ipowfs].lotf->amp 
	=calloc(nx*nx,sizeof(double));
    double (*amps)[nx]
	=(double(*)[nx])powfs[ipowfs].lotf->amp;
    double l2max
	=pow(parms->powfs[ipowfs].llt->d*0.5,2);
    double r2eff=l2max
	*pow(parms->powfs[ipowfs].llt->widthp,2);
    double sumamp2=0;
    for(int iy=0; iy<nx; iy++){
	double yy=iy*dx+oy;
	yy*=yy;
	for(int ix=0; ix<nx; ix++){
	    double xx=ix*dx+ox;
	    xx*=xx;
	    double r2=xx+yy;
	    if(r2<=l2max){
		amps[iy][ix]=exp(-r2/r2eff);
		sumamp2+=pow(amps[iy][ix],2);
	    }
	}
    }
    //normalized
    //so that max(otf)=1;
    sumamp2=1./(sqrt(sumamp2));
    for(int i=0; i<nx*nx; i++){
	powfs[ipowfs].lotf->amp[i]*=sumamp2;
    }
    powfs[ipowfs].lotf->loc=mksqloc(nx,nx,dx,
				    lpts->origx[0],
				    lpts->origy[0]);
    powfs[ipowfs].lotf->mcc
	=pts_mcc_ptt(powfs[ipowfs].lotf->pts,
		     powfs[ipowfs].lotf->amp);
    powfs[ipowfs].lotf->imcc
	=dcellinvspd_each(powfs[ipowfs].lotf->mcc);

    if(parms->save.setup){
	locwrite(powfs[ipowfs].lotf->loc, "%s/powfs%d_llt_loc",dirsetup,ipowfs);
	writedbl(powfs[ipowfs].lotf->amp,powfs[ipowfs].pts->nx,
		 powfs[ipowfs].pts->nx, "%s/powfs%d_llt_amp", dirsetup,ipowfs);
	dcellwrite(powfs[ipowfs].srot, "%s/powfs%d_srot",dirsetup,ipowfs);
	dcellwrite(powfs[ipowfs].srsa, "%s/powfs%d_srsa",dirsetup,ipowfs);
	dcellwrite(powfs[ipowfs].lotf->imcc, "%s/powfs%d_llt_imcc",dirsetup,ipowfs);
	for(int iwvl=0; iwvl<nwvl; iwvl++){
	    if(powfs[ipowfs].etfprep[iwvl].p1){
		ccellwrite(powfs[ipowfs].etfprep[iwvl].p1, 
			   "%s/powfs%d_etfprep%d_1d.bin.gz",dirsetup,ipowfs,iwvl);
	    }
	    if(powfs[ipowfs].etfprep[iwvl].p2){
		ccellwrite(powfs[ipowfs].etfprep[iwvl].p2,
			   "%s/powfs%d_etfprep%d_2d.bin.gz",dirsetup,ipowfs,iwvl);
	    }
	}
	if(powfs[ipowfs].etfsim != powfs[ipowfs].etfsim){
	    for(int iwvl=0; iwvl<nwvl; iwvl++){
		if(powfs[ipowfs].etfsim[iwvl].p1){
		    ccellwrite(powfs[ipowfs].etfsim[iwvl].p1, 
			       "%s/powfs%d_etfsim%d_1d.bin.gz",dirsetup,ipowfs,iwvl);
		}
		if(powfs[ipowfs].etfsim[iwvl].p2){
		    ccellwrite(powfs[ipowfs].etfsim[iwvl].p2,
			       "%s/powfs%d_etfsim%d_2d.bin.gz",dirsetup,ipowfs,iwvl);
		}
	    }
	}
    }
}
/**
   Setup the matched filter pixel processing parameters for physical optics wfs.
 */
static void 
setup_powfs_mtch(POWFS_T *powfs,const PARMS_T *parms, 
		 int ipowfs){
    if(parms->powfs[ipowfs].phytype!=1){//not matched filter
	error("This is only intended for matched filter\n");
    }
    long nsa=powfs[ipowfs].pts->nsa;
    if(powfs[ipowfs].intstat){
	dcellfree(powfs[ipowfs].intstat->mtche);
	dcellfree(powfs[ipowfs].intstat->sanea);
	dcellfree(powfs[ipowfs].intstat->saneaixy);
	dfree(powfs[ipowfs].intstat->i0sum);
	free(powfs[ipowfs].intstat);
    }
    INTSTAT_T *intstat=powfs[ipowfs].intstat=calloc(1, sizeof(INTSTAT_T));
    if(parms->load.i0){
	warning("Loading i0, gx, gy\n");
	intstat->i0=dcellread("powfs%d_i0.bin.gz",ipowfs);
	intstat->gx=dcellread("powfs%d_gx.bin.gz",ipowfs);
	intstat->gy=dcellread("powfs%d_gy.bin.gz",ipowfs);
    }else{
	if(parms->powfs[ipowfs].piinfile){
	    //load psf. 1 for each wavefront sensor.
	    info2("Using 1 sepsf for each wfs when loading sepsf\n");
	    intstat->nsepsf=parms->powfs[ipowfs].nwfs;
	    intstat->sepsf=calloc(intstat->nsepsf, sizeof(dcell*));
	    for(int jwfs=0; jwfs<parms->powfs[ipowfs].nwfs; jwfs++){
		int iwfs=parms->powfs[ipowfs].wfs[jwfs];
		intstat->sepsf[jwfs]=dcellread("%s_wfs%d",parms->powfs[ipowfs].piinfile,iwfs);
		double pmax=dmax(intstat->sepsf[jwfs]->p[0]);
		if(intstat->sepsf[jwfs]->p[0]->p[0]>pmax/2){
		    error("wfs %d:  psf must have peak at center, not corner.\n", iwfs);
		}
		if(intstat->sepsf[jwfs]->nx!=nsa){
		    error("piinfile doesn't match\n");
		}
	    }
	}else if(parms->powfs[ipowfs].lo){
	    error("Please specify piinfile for lo order phy wfs\n");
	}else{
	    //LGS
    	    int npsfx=powfs[ipowfs].pts->nx
		*parms->powfs[ipowfs].embfac;
	    int npsfy=npsfx;
	    char *fnprefix;
	    if(parms->aper.fnamp){
		char *tmp=mybasename(parms->aper.fnamp);
		uint32_t key=hashlittle(powfs[ipowfs].amp,
					powfs[ipowfs].loc->nloc*sizeof(double),0);
	
		char tmp2[80];
		snprintf(tmp2,80,"_%ud",key);
		fnprefix=stradd(tmp,tmp2,NULL);
		free(tmp);
	    }else{
		fnprefix=strdup("noamp");
	    }
	    if(parms->powfs[ipowfs].ncpa && parms->powfs[ipowfs].ncpa_method==2){
		char *tmp=fnprefix;
		uint32_t key=0;
		for(int iwfs=0; iwfs<parms->powfs[ipowfs].nwfs; iwfs++){
		    key=hashlittle(powfs[ipowfs].ncpa->p[iwfs]->p,
				   powfs[ipowfs].loc->nloc*sizeof(double),key);
		}
		char tmp2[80];
		snprintf(tmp2,80,"_%ud",key);
		fnprefix=stradd(tmp,tmp2,NULL);
		free(tmp);
	    }
	    
	    char fnotf[PATH_MAX];
	    snprintf(fnotf,PATH_MAX,"%s/.aos/otfc/",HOME);
	    if(!exist(fnotf)) 
		mymkdir("%s",fnotf);
	    if(parms->powfs[ipowfs].ncpa && parms->powfs[ipowfs].ncpa_method==2){
		intstat->notf=parms->powfs[ipowfs].nwfs;
	    }else{
		intstat->notf=1;
	    }
	    intstat->otf=calloc(intstat->notf, sizeof(ccell*));
	    int calc_otf=0;
	    for(int iotf=0; iotf<intstat->notf; iotf++){
		snprintf(fnotf,PATH_MAX,"%s/.aos/otfc/%s_D%g_%g_"
			 "r0_%g_L0%g_dsa%g_nsa%ld_dx1_%g_"
			 "nwvl%d_%g_embfac%d_%dx%d_SEOTF_%d_v1.bin.gz",
			 HOME, fnprefix,
			 parms->aper.d,parms->aper.din, 
			 parms->atm.r0, parms->atm.l0, 
			 powfs[ipowfs].pts->dsa,nsa,
			 1./powfs[ipowfs].pts->dx, 
			 parms->powfs[ipowfs].nwvl,
			 parms->powfs[ipowfs].wvl[0]*1.e6,
			 parms->powfs[ipowfs].embfac,npsfx,npsfy, iotf);
		if(exist(fnotf)){
		    info2("Loading otf from %s\n",fnotf);
		    intstat->otf[iotf]=ccellread("%s",fnotf);
		    touch(fnotf);
		}else{
		    calc_otf=1;
		    break;
		}
	    }
	    if(calc_otf){
		info2("Not found:%s\nGenerating seotf...",fnotf);tic;
		genseotf(parms,powfs,ipowfs);
		toc2("done");
		for(int iotf=0; iotf<intstat->notf; iotf++){
		    snprintf(fnotf,PATH_MAX,"%s/.aos/otfc/%s_D%g_%g_"
			     "r0_%g_L0%g_dsa%g_nsa%ld_dx1_%g_"
			     "nwvl%d_%g_embfac%d_%dx%d_SEOTF_%d_v1.bin.gz",
			     HOME, fnprefix,
			     parms->aper.d,parms->aper.din, 
			     parms->atm.r0, parms->atm.l0, 
			     powfs[ipowfs].pts->dsa,nsa,
			     1./powfs[ipowfs].pts->dx, 
			     parms->powfs[ipowfs].nwvl,
			     parms->powfs[ipowfs].wvl[0]*1.e6,
			     parms->powfs[ipowfs].embfac,npsfx,npsfy, iotf);
		    info2("saving otf to %s\n", fnotf);
		    ccellwrite(intstat->otf[iotf], "%s",fnotf);
		}
	    }
	 
	    if(parms->powfs[ipowfs].hasllt){
		char fnlotf[PATH_MAX];
		snprintf(fnlotf,PATH_MAX,"%s/.aos/otfc/%s_D%g_%g_"
			 "r0_%g_L0%g_dsa%g_lltd%g_dx1_%g_"
			 "nwvl%d_%g_embfac%d_%dx%d_SELOTF_v1.bin.gz", 
			 HOME, fnprefix,
			 parms->aper.d,parms->aper.din, 
			 parms->atm.r0, parms->atm.l0, 
			 powfs[ipowfs].lotf->pts->dsa,
			 parms->powfs[ipowfs].llt->d,
			 1./powfs[ipowfs].pts->dx, 
			 parms->powfs[ipowfs].nwvl,
			 parms->powfs[ipowfs].wvl[0]*1.e6,
			 parms->powfs[ipowfs].embfac,npsfx,npsfy);
		if(exist(fnlotf)){
		    intstat->lotf=ccellread("%s",fnlotf);
		    touch (fnlotf);
		}else{
		    genselotf(parms,powfs,ipowfs);
		    ccellwrite(intstat->lotf, "%s",fnlotf);
		}
	    }
	    free(fnprefix);
	    //Generating short exposure images.
	    gensepsf(parms,powfs,ipowfs);
	    if(parms->save.setup && intstat){
		dcellwrite(intstat->sepsf[0],
			   "%s/powfs%d_sepsf",dirsetup,ipowfs);
	    }
	    //Free short exposure otf.
	    ccellfree(intstat->lotf);
	    for(int iotf=0; iotf<intstat->notf; iotf++){
		ccellfree(intstat->otf[iotf]);
	    }
	    free(intstat->otf);
	}
	//generate short exposure i0,gx,gy from psf.
	gensei(parms,powfs,ipowfs);
	dcellfreearr(intstat->sepsf,intstat->nsepsf);
	if(parms->save.setup && intstat){
	    dcellwrite(intstat->i0,"%s/powfs%d_i0",dirsetup,ipowfs);
	    dcellwrite(intstat->gx,"%s/powfs%d_gx",dirsetup,ipowfs);
	    dcellwrite(intstat->gy,"%s/powfs%d_gy",dirsetup,ipowfs);
	}
    }
    //Generating Matched filter
    genmtch(parms,powfs,ipowfs);
    dcellfree(intstat->i0);
    dcellfree(intstat->gx);
    dcellfree(intstat->gy);
    dcellfree(intstat->saneara);
    dcellfree(intstat->saneaxy);
    //Remove OTFs that are older than 30 days.
    char *dirotf=stradd(HOME, "/.aos/otfc", NULL);
    remove_file_older(dirotf, 30*24*3600);
    free(dirotf);
}
/**
   Setup the powfs struct based on parms and aper. Everything about wfs are
setup here.  \callgraph */
POWFS_T * setup_powfs(const PARMS_T *parms, APER_T *aper){
    POWFS_T *powfs=calloc(parms->npowfs, sizeof(POWFS_T));
    int ipowfs;
    tic;
    for(ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
	if(parms->powfs[ipowfs].nwfs==0) continue;
	setup_powfs_geom(powfs,parms,aper,ipowfs);
        setup_powfs_grad(powfs,parms,ipowfs);
	setup_powfs_ncpa(powfs,parms,ipowfs);
	if(TEST_POWFS||parms->powfs[ipowfs].usephy
	   ||parms->powfs[ipowfs].psfout
	   ||parms->powfs[ipowfs].pistatout
	   ||parms->powfs[ipowfs].neaphy){
	    /*
	    if(parms->powfs[ipowfs].pistatout 
	       &&parms->powfs[ipowfs].usephy){
		error("Must use geometric grad for pistatout\n");
	    }
	    */
	    //We have physical optics. setup necessary struct
	    setup_powfs_prep_phy(powfs,parms,ipowfs);
	    setup_powfs_dtf(powfs,parms,ipowfs);
	    if(parms->powfs[ipowfs].hasllt){
		//prepare Laser launch telescope.
		setup_powfs_sodium(powfs,parms,ipowfs);//read sodium profile and smooth it
		setup_powfs_etf(powfs,parms,ipowfs,0,0);//etf for prep
		setup_powfs_etf(powfs,parms,ipowfs,1,0);//etf for sim
		setup_powfs_llt(powfs,parms,ipowfs);
	    }
	}
	if(parms->powfs[ipowfs].hasllt){
	    //If there is LLT, setup the extra focus term if needed.
	    setup_powfs_focus(powfs,parms,ipowfs);
	}
	if(TEST_POWFS||parms->powfs[ipowfs].usephy || parms->powfs[ipowfs].neaphy){
	    if(parms->powfs[ipowfs].phytype==1){//matched filter
		setup_powfs_mtch(powfs,parms,ipowfs);
	    }else{
		error("Please fill in this part\n");
	    }
	}
    }//ipowfs
    toc("setup_powfs");
    return powfs;
}
/**
   Free all parameters of powfs at the end of simulation.
 */
void free_powfs(const PARMS_T *parms, POWFS_T *powfs){
    int ipowfs;
    for(ipowfs=0; ipowfs<parms->npowfs; ipowfs++){
	free_powfs_geom(powfs, parms, ipowfs);
	free_powfs_dtf(powfs, parms, ipowfs);
	spfree(powfs[ipowfs].GS0);
	spfree(powfs[ipowfs].ZS0);
	if(powfs[ipowfs].intstat){
	    dcellfree(powfs[ipowfs].intstat->mtche);
	    dcellfree(powfs[ipowfs].intstat->sanea);
	    dcellfree(powfs[ipowfs].intstat->saneaixy);
	    dfree(powfs[ipowfs].intstat->i0sum);
	    free(powfs[ipowfs].intstat);
	}
	if(parms->powfs[ipowfs].hasllt){
	    dcellfree(powfs[ipowfs].srot);
	    dcellfree(powfs[ipowfs].srsa);
	    dfree(powfs[ipowfs].srsamax);
	    dcellfree(powfs[ipowfs].sprint);
	}
	dcellfree(powfs[ipowfs].mcc);
	dcellfree(powfs[ipowfs].imcc);
	if(parms->powfs[ipowfs].hasGS0){
	    spfree(powfs[ipowfs].GS0);
	}
	if(powfs[ipowfs].lotf){
	    ptsfree(powfs[ipowfs].lotf->pts);
	    free(powfs[ipowfs].lotf->amp);
	    locfree(powfs[ipowfs].lotf->loc);
	    dcellfree(powfs[ipowfs].lotf->mcc);
	    dcellfree(powfs[ipowfs].lotf->imcc);
	    free(powfs[ipowfs].lotf);
	}
	dfree(powfs[ipowfs].sodium);
	if(powfs[ipowfs].etfprep){
	    if(powfs[ipowfs].etfprep!=powfs[ipowfs].etfsim){
		for(int iwvl=0;iwvl<parms->powfs[ipowfs].nwvl;iwvl++){
		    ccellfree(powfs[ipowfs].etfsim[iwvl].p1);
		    ccellfree(powfs[ipowfs].etfsim[iwvl].p2);
		}
		free(powfs[ipowfs].etfsim);
	    }
	    for(int iwvl=0;iwvl<parms->powfs[ipowfs].nwvl;iwvl++){
		ccellfree(powfs[ipowfs].etfprep[iwvl].p1);
		ccellfree(powfs[ipowfs].etfprep[iwvl].p2);
	    }
	    free(powfs[ipowfs].etfprep);
	}
    }
    free(powfs);
}