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
#ifndef SKYC_PARMS_H
#define SKYC_PARMS_H
#include "photon.h"
/**
   \file skyc/parms.h
   Parameters for simulation.
*/
/** Parameters for skycoverage simulation relates to high order
   pre-processing. Some of the parameters are to be exported by MAOS. The rest
   are to be supplied by the end user.  */

typedef struct maos_s{
    real r0z;    /**<Fried parameter, used to read in PSD information.*/
    real dt;     /**<simulation period time. 1/800*/
    real zadeg;  /**<Zenith angle in degree*/
    real za;     /**<Derived: zenith angle*/
    real hc;     /**<Height conjugation of upper DM*/
    real hs;     /**<Height of LGS*/
    real D;      /**<Diameter of telescope*/
    real *wvl;   /**<Wavelength vector*/
    int nwvl;      /**<Number of wavelength*/
    int npowfs;    /**<Number of types of wfs.*/
    int nmod;      /**<Number of modes controlled by LOW NGS WFS;*/
    int *seeds;    /**<List of seed for maos PSF outputs.*/
    int nseed;     /**<Number of seeds*/
    real *wddeg; /**<wind direction list for each layer. just to propagate to
		      maos for integrated simulation.*/
    int nwddeg;    /**<number of wddeg*/

    /*The following are vectors for each powfs. */
    int *nsa;      /**<total number of subapertures*/
    int *msa;      /**<order of correction in 1-d*/
    int *ncomp;    /**<size of saved psf/wvf*/
    int *embfac;   /**<embeding factor of saved psf. determines the PSF sampling.*/
    real *dxsa;  /**<subaperture size*/
    char **fnwfsloc; /**<file name for WFS grid*/
    char **fnwfsamp; /**<file name for WFS amptitude defined on grid*/
    char **fnsaloc;  /**<file name for subaperture location coordinate*/
    char *fnmideal;  /**<file name for ideal NGS modes*/
    char *fnmidealp; /**<file name for ideal NGS mode computation for each direction*/
    int evlindoa;    /**<index of the On axis evaluation point.*/
    dmat *mcc;       /**<NGS modal cross coupling matrix.*/
    dmat *mcc_oa;    /**<NGS modal cross coupling matrix for on axis only.*/
    dmat *mcc_tt;    /**<NGS tip/tilt modal cross coupling matrix*/
    dmat *mcc_oa_tt; /**<NGS tip/tilt modal cross coupling matrix for on axis only, 2x2*/
    dmat *mcc_oa_tt2;/**<modal cross coupling matrix between NGS and tip/tilt for on axis only, 2x5*/
    real ngsgrid;  /**<spacing of NGS grid in as*/
    int nstep;       /**<Number of time steps (sim.nsim) in MAOS simulation*/
    int ahstfocus;   /**<1: The focal plate scale mode does not include global focus*/
    int mffocus;     /**<1: maos already does sodium tracking*/
    int indfocus;    /**<Index of focus mode. 0: disable*/
    int indps;       /**<Index of plate scale mode. 0: disable*/
    int indastig;    /**<Index of astigmatism mode. 0: disable*/
    char *fnrange;   /**<sodium range time series. forward to maos.*/
}maos_s;

/**
   Contains parameters for skycoverage.
*/
typedef struct skyc_s{
    ZB_S zb;         /**<Sky background and zero magnitude flux*/
    int verbose;     /**<be verbose at output.*/
    //int reest;       /**<reestimate the error after gain estimation.*/
    int dbg;         /**<save intermediate information for debugging.*/
    int dbgsky;      /**<only run this sky frame if not -1*/
    int dbgaster;    /**<only run this asterism if not -1*/
    int keeporder;   /**<1: Keep order of stars as input is skyc.star.*/
    int interpg;     /**<Interpolate gain based in dtrat and signan*/
    int save;        /**<save information for latter running in MAOS.*/
    int maxstar;     /**<maximum number of (brightest) stars for each star field.*/
    int maxaster;    /**<maximum number of best asterism for final processing.*/
    int maxdtrat;    /**<maximum number of dtrats to try for each asterism.*/
    int nthread;     /**<number of threads*/
    int start;       /**<start number of sky field*/
    int nsky;        /**<number of sky field. 500.*/
    int seed;        /**<seed for generating asterism and random numbers.*/
    int navg;        /**<Number of simulations to average*/
    int noisefull;   /**<use full noise insteaded servo filtered noise to regularize. =0*/
    int psd_scale;   /**<scale the PSD to equal to open loop error*/
    int noisy;       /**<noise simulations.*/
    int ttfbrightest;/**<make ttf the brightest always.*/
    int ttffastest; /**<make ttf the fastest always in multirate.*/
    int bspstrehl;   /**<Do bicubic spline interpolation on strehl*/
    int npowfs;      /**<number of powfs, has to match maos_s.npowfs*/
    real lat;      /**<Galactic latitude*/
    real lon;      /**<Galactic longitude.*/
    real catscl;   /**<Scale the catlog star count*/
	real maglimit;	 /**<If set, remove stars dimmer that this in the first wavelength. */
    real patfov;   /**<Patrol FoV in arcsec (diameter)*/
    real minrad;   /**<minimum radius of the stars (keep out of center science region)*/
    real imperrnm; /**<Implementation error in nm in the center.*/
    real imperrnmb;/**<Implementation error slopt in nm:
			imperrnm(theta)=sqrt(imperrnm^2+imperrnmb^2*theta^2). The
			location in fov, theta, is normalized by patfov/2*/
    /** The following are vectors for each powfs; */
    lmat *nwfsmax;    /**<maximum # of wfs for each powfs*/
    int  nwfstot;    /**<Sum of nwfsmax*/
    real *pixtheta;/**<size of WFS ccd pixel.*/
    real *pixblur; /**<blurring of pixels.*/
    int *pixpsa;     /**<number of detector pixels in each direction per sa*/
    int *pixguard;   /**<guard window size in each direction.*/
    real *pixoffx; /**<pixel offset along x in units of pixel*/
    real *pixoffy; /**<pixel offset along y in units of pixel*/
    real keepout;  /**<NGS probe keep out range in arcsec.*/
    real rne;      /**<detector read out noise in electron. -1 to use the formula.*/
    real excess;   /**<detector excess photon noise factor*/
    dmat *rnefs;     /**<derived, actual read out noise, may be frame rate dependent.*/
    //real *telthruput;/**<Telescope throughput at each wvl*/
    //real *qe;      
    //real *excessbkgrnd;
    real wvlmean;  /**<Mean wavelength*/
    dmat *wvlwt;     /**<Weight for each wavelength*/
    int ngsalign;    /**<align NGS to the grid.*/
    int limitnstep;  /**<Limit the number of steps in each simulation*/
    int evlstart;    /**<time step to start evaluate performance*/
    int phystart;    /**<time step to start physical optics*/
    int mtchcr;      /**<constraint in matched filter*/
    int mtchfft;     /**<Use FFT for derivative*/
    int phytype;     /**<Type of pixel processing. 1: mtch, 2: cog, 3: correlation*/
    int neaaniso;     /**<use additional measurement error caused by anisoplanatism in regularization.*/
    int neanonlin;   /**<use additional measurement error caused by WFS nonlinearity*/
    int ndtrat;      /**<number of dtrat*/
    dmat *dtrats;     /**<ratio between NGS and LGS WFS sampling period. use dmat due to interp1 calling*/
    dmat *snrmin_fast; /**<#minimum snr to select each dtrats_mr. balances servo lag error vs noise propagation*/
	dmat *snrmin_slow; /**<#minimum snr to select each dtrats_mr. balances servo lag error vs noise propagation*/
    dmat *fss;       /**<sampling frequency at each dtrat*/
    int servo;       /**<servo type of NGS LOOP. 2: type II*/
    int ngain;       /**<Number of parameters for gain*/
    int gsplit;      /**<use separate gains for tip/tilt and plate scale modes*/
    dmat *psd_ws;    /**<PSD of windshake*/
    
    char *stars;     /**<file name of not NULL to load stars from*/
    int addws;       /**<add wind shake time series to simulation*/
    real pmargin;    /**<phase margin of type II*/
    int psdcalc;     /**<Calculate PSD from time series*/
    char **fnpsf1;   /**<file name for additional otf to be interpolated and
			multiplied to dtfq. 2 columns. first column is coordinate
			of otf, and second column of value.*/
    real sdetmax;  /**<tmax for SDE fitting*/
    int multirate;   /**<Each OIWFS can run at different dtrat*/
    real snrmin;   	 /**<Minimum SNR to determine minimum dtrat. SNR computed as pixtheta/nea*/
    int usephygrad;  /**<1: Use physical optics grad instead of ztilt*/
    int estimate;    /**<1: Estiamte performance only, without time domain simulation*/
}skyc_s;
/**
   Parameters for skycoverage.
 */
typedef struct parms_s{
    maos_s maos;     /**<parameters exported by maos*/
    skyc_s skyc;     /**<parameters supplied by user*/
    int *fdlock;     /**<Records the fd of the seed lock file. if -1 will skip the seed*/
	char **fnlock;   /**<Records the filename of the seed lock file. if -1 will skip the seed*/
}parms_s;
/**
   arg_s is used for command line parsing.
*/
typedef struct arg_s{
    int detach;      /**<Detach from the command line and run in background*/
    int override;    /**<Override result even if Res?_?.done exist*/
    int force;       /**<For start, bypassing scheduler*/
    int nthread;     /**<Number of threads*/
    char *dirout;    /**<Result output directory*/
    char *conf;      /**master .conf file. nfiraos.conf by default. -c to change*/
    char *confcmd;
}arg_s;

parms_s *setup_parms(const arg_s *arg);
void free_parms(parms_s *parms);
#endif
