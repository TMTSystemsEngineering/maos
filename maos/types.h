/*
  Copyright 2009, 2010, 2011 Lianqi Wang <lianqiw@gmail.com> <lianqiw@tmt.org>
  
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

#ifndef __AOS_TYPES_H__
#define __AOS_TYPES_H__
#if USE_PTHREAD > 0
#include <pthread.h>
#endif
/**
   \file maos/types.h
   A few run time structs
*/
/**
   contains the data associated with the aperture and performance
   evaluation.
 */
typedef struct APER_T{
    loc_t *locs;         /**<PLOCS in laos. the fine sampled grid on aperture
			    for peformance evaluation.*/
    dmat *amp;           /**<amplitude map defined on locs, if exists. sum to 1. for
			    performance evaluation*/
    dmat *amp1;          /**<amplitude map defined o locs, maximum is 1. use for plotting.*/
    map_t *ampground;    /**<The input amplitude map on ground level read from file.*/
    dmat *mod;           /**<modal columne vectors if parms->evl.nmax>1*/
    dmat *mcc;           /*piston/tip/tilt mode cross-coupling for evaluations.*/
    dmat *imcc;          /**<inverse of piston/tip/tilt mode cross-coupling for evaluations.*/
    double ipcc;         /**<piston term in imcc.*/
    double sumamp2;      /**<sum of amplitude squared*/
    long **embed;         /**<Embedding index for PSF computing, one per wvl*/
    long *nembed;         /**<dimension of embed.*/
    double fcp;          /**<piston correction in focus term.*/
}APER_T;
/**
   contains the data associated with a detector transfer function for a
   subaperture. The PSF is computed as
   \f$\textrm{PSF}=\frac{1}{N^2\sum(\textrm{amp}^2)}|\textrm{fftshift}\mathcal{F}[A
   \exp(-\frac{2\pi}{\lambda}\textrm{opd})]|^2\f$.  The subaperture image is
   computed as
   \f$I=\textrm{si}*\mathcal{F}^{-1}[\mathcal{F}[\textrm{PSF}\times\textrm{nominal}]]\f$
*/
typedef struct DTF_T{
    ccell *nominal;      /**<The FFT of the pixel functions*/
    spcell *si;          /**<The pixel selection*/
    cmat *Ux;            /**<Special frequency vector along x*/
    cmat *Uy;            /**<Special frequency vector along y*/
    int fused;           /**<Whether the DTF has been fused to ETF*/
}DTF_T;
/**
   contains the data associated with an elongation transfer function for a
subaperture.  */
typedef struct ETF_T{
    ccell *p1;          /**<Store the ETF along radial direction when radrot==1*/
    ccell *p2;          /**<Store the 2D ETF when radrot==0*/
}ETF_T;
/**
   contains the data associated with a LLT uplink path.
 */
typedef struct LLT_T{
    loc_t *loc;          /**<The grid that defines the LLT pupil*/
    pts_t *pts;          /**<The LLT lower left grid point location*/
    dmat  *amp;          /**<The amplitude defined on loc*/
    dcell *mcc;          /**<modal cross coupling matrix*/
    dcell *imcc;         /**<inverse of imcc*/
    dcell *ncpa;         /**<The LLT surface error*/
}LLT_T;
/**
   contains the intensity statistics assiciated with a certain powfs for
physical optics wfs. */
typedef struct INTSTAT_T{
    ccell *lotf;        /**<llt otf*/
    ccell **otf;        /**<short exposure OTF. time consuming to calculate. */
    dcell **sepsf;      /**<short expsoure PSF.*/
    dcell *i0;          /**<short exposure image. nsa x nllt*/
    dcell *gx;          /**<gradient of i0 along x*/
    dcell *gy;          /**<gradient of i0 along y*/
    dmat *i0sum;        /**<sum of i0*/
    dcell *mtchera;     /**<mtched filter operator along r/a*/
    dcell *mtche;       /**<mtched filter operator along x/y*/
    dcell *saneara;     /**<computed sanea along ra (rad^2) (only for radian coord ccd)*/
    dcell *sanea;       /**<SANEA of matched filter output discarding
			   coupling. ra or xy (rad^2). Just for saving.*/
    dcell *saneaxy;     /**<computed sanea along xy. (rad^2)*/
    dcell *saneaixy;    /**<computed sanea inverse along xy (rad^-2)*/
    dcell *saneaxyl;    /**<decomposition of saneaxy: L*L'=saneaxy.*/
    int notf;           /**<number of otf; 1 unless there is ncpa.*/
    int nsepsf;         /**<number of sepsf; usually 1.*/
    int nmtche;         /**<number of matched filters. 1 or nwfs of this powfs.*/
}INTSTAT_T;
/**
   contains the data associated with a certain type of WFS. not
necessarily physical optics WFS.x */
typedef struct POWFS_T{
    //Parameters about subapertures.
    loc_t *saloc;       /**<lower left corner of the subaperture*/
    pts_t *pts;         /**<records lower left-most point of each sa in a regular grid.*/
    dmat *saa;          /**<Subaperture area*/
    loc_t *loc;         /**<concatenated points for all subapertures.*/
    loc_t **locm;       /**<mis-registered loc, if any.*/
    dcell *saam;        /**<mis-registered saa, if any*/
    dmat *amp;          /**<amplitude map, max at 1.*/
    dcell *ampm;        /**<real amplitude map on misregistered grid, locm. used for gradient computing*/
    double areascale;   /**<1./max(area noramlized by dsa*dsa)*/
    double (*misreg)[2];/**<pure misregistration taken from parms->powfs[ipowfs].misreg*/
    //NCPA
    dcell *ncpa;        /**<NCPA OPDs to add to WFS OPD during simulation.*/
    dcell *ncpa_grad;   /**<NCPA grads to subtract from measurement. */
    //Physical optics
    DTF_T *dtf;         /**<array of dtf for each wvl*/
    //LGS Physical Optics
    dmat *sodium;       /**<Loaded and downsampled sodium profile from the file.*/
    ETF_T *etfprep;     /**<ETF for computing short exposure matched filter.*/
    ETF_T *etfsim;      /**<ETF for simulation.*/
    dmat *focus;        /**<additional focus error. (llt->fnrange)*/
    LLT_T *llt;         /**<uplink aperture parameters*/
    dcell **saimcc;     /**<inverse of p/t/t model cross coupling matrix for each subaps to compute ztilt.*/
    dcell *srot;        /**<subaperture rotation wrt LLT*/
    dcell *srsa;        /**<subaperture distance wrt LLT*/
    dmat *srsamax;      /**<max of srsa for each llt.*/
    //Geometric gradient
    spcell *GS0;        /**<gtilt (average gradient) on ampm*/
    dcell *neasim;      /**<NEA in radian, at dtrat, to be used in simulation
			   for geometric wfs model.*/
    //Matched filter
    dcell *sprint;      /**<which subapertures to print sanea*/
    INTSTAT_T *intstat; /**<matched filter i0 and its derivative.*/
    dmat *dtheta;       /**<sampling of the imaging fft grid. wvl/(embfac*dxsa);*/
    dcell *bkgrnd;      /**<wfs background image. from parms->powfs[ipowfs].bkgrndfn.*/
    dcell *bkgrndc;     /**<wfs background image calibration. from parms->powfs[ipowfs].bkgrndfnc.*/
    int nwfs;           /**<number of wfs belonging to this wfs*/
    int npts;           /**<number of total opd points in all subaps*/
    int namp;           /**<number of amplitude maps*/
    int pixpsax;        /**<number of detector pixels along x*/
    int pixpsay;        /**<number of detector pixels along y*/
    int ncompx;         /**<Dimension of FFT for subaperture imaging along x*/
    int ncompy;         /**<Dimension of FFT for subaperture imaging along y*/
    int nlocm;          /**<number of misregistered loc. 1 or nwfs of this powfs.*/
    int nimcc;         /**<number of saimcc*/
    int nthread;        /**<Equal to MAX(nsa,sim.nthread)*/
    //The following are a few convenient pointers.
    double **realamp;   /**<The real (after misregisteration/distortion) amplitude map*/
    double **realsaa;   /**<The real (after misregisteration/distortion) subaperture area*/
    //For PSF computation on location of WFS. 
    long **embed;         /**<Embedding index for PSF computing, one per wvl*/
    long *nembed;         /**<dimension of embed.*/
    dmat *sumamp;        /**<sum of realamp*/
    dmat *sumamp2;       /**<sum of realamp.^2*/
    dcell *mcc;          /**<p/t/t model cross coupling matrix for entire pupil*/
    dcell *imcc;         /**<inverse of p/t/t model cross coupling matrix for entire pupil*/
    dmat *ipcc;          /**<inverse of piston model cross coupling matrix for entire pupil*/
}
POWFS_T;

/**
   NGS mode and reconstructors in ad hoc split tomography. Only useful with 1 or 2 DMs.
 */
typedef struct NGSMOD_T{
    double hs;      /**<height of LGS*/
    double ht;      /**<height of upper DM.*/
    double scale;   /**<(1-ht/hs)^-2*/
    double aper_fcp;/**<piston term in focus in plocs.*/
    dcell *MCCP;    /**<cross coupling of the NGS modes for each direction. Hm'*W*Hm*/
    dmat *MCC;      /**<cross coupling of the NGS modes. 2x2 for 1 dm. 5x5 for 2 dms*/
    dmat *IMCC_TT;  /**<inv of cross coupling of tip/tilt modes only.*/
    dmat *IMCC;     /**<inv of MCC.*/
    dcell *GM;      /**<ngsmod vector to gradient operator*/
    dcell *Rngs;    /**<NGS reconstructor from NGS grad to NGS mod vec. pinv of GM*/
    dcell *Pngs;    /**<Project DM command to NGS modes */
    dcell *Modes;   /**<DM vector for the modes*/
    dcell *Ptt;     /**<Invidual DM tip/tilt removal.*/
    spcell *Wa;     /**<Aperture weighting. Ha'*W*Ha. It has zeros in diagonal. Add tikholnov*/
    int nmod;       /**<nmod: 5 for 2 dm, 2 for 1 dm.*/
}NGSMOD_T;
/**
   contains data for Fourier Domain Preconditioner.
 */
typedef struct FDPCG_T{
    loc_t **xloc;  /**<record recon->xloc*/
    csp *Minv;     /**<inverse of fourier domain preconditioner matrix M.*/
    ccell *Mbinv;  /**<block version of Minv. (in permuted order)*/
    long *perm;    /**<Permutation vector to get block diagonal matrix*/
    long nxtot;    /**<Total number of reconstructed phase points*/
    cmat *xhat;    /**<Intermediate matrices*/
    cmat *xhat2;   /**<Intermediate matrices*/
    ccell *xhati;  /**<Intermediate matrices*/
    ccell *xhat2i; /**<Intermediate matrices*/
    long **xembed; /**<index to embed nonsquare opd on xloc to square map.*/
    double *scale; /**<Scaling factor for each layer*/
    int square;    /**<Whether xloc is square*/
}FDPCG_T;

/**
   contains MOAO related data
*/
typedef struct MOAO_T{
    int used;         /**<Whether this MOAO is used or not*/
    loc_t *aloc;      /**<Actuator grid*/
    spcell *HA;       /**<Propagator from this aloc to PLOC*/
    dcell *NW;        /**<null modes and constraints*/
    dmat *W1;         /**<Weighting matrix on PLOC. same as recon->W1*/
    dsp *W0;          /**<Weighting matrix on PLOC. same as recon->W0*/
    spcell *actslave; /**<Slaving operator for actuators not illuminated*/
    dmat *aimcc;      /**<used for tip/tilt removal from DM commands.*/
    icell *actstuck;  /**<stuck actuators*/
    icell *actfloat;  /**<floating actuators*/
}MOAO_T;
/**
   A convenient wrap of the data to embed into MUV_T for applying invpsd to opds defined on xloc.
*/
typedef struct INVPSD_T{
    dcell  *invpsd;    /**<inverse of the turbulence PSF*/
    ccell  *fftxopd;   /**<temporary array to apply inverse PSD in Fourier domain.*/
    loc_t **xloc;      /**<points to recon->xloc*/
    int     square;    /**<whether opd is on square xloc or not.*/
}INVPSD_T;
/**
   A convenient wrap of the data to embed into MUV_T for applying fractal
   regularization to opds defined on xloc.
 */
typedef struct FRACTAL_T{
    dcell  *xopd;      /**<A square array to embed x on xloc into for fractal */
    loc_t **xloc;      /**<points to recon->xloc*/
    const double *wt;  /**<weight of each layer*/
    double  r0;        /**<The Fried parameter*/
    double  l0;        /**<The outer scale*/
    double  scale;     /**<An additional scaling factor*/
    long   ninit;      /**<The initial size to do with covariance matrix. 2 is minimum*/
}FRACTAL_T;
typedef struct CN2EST_T CN2EST_T;

/**
   contains data related to wavefront reconstruction and DM fitting.  */
typedef struct RECON_T{
    const PARMS_T *parms;/**<Save a const pointer of parms*/
    struct SIM_T *simu;       /**<Temporarily save a pointer to simu*/
    double r0;         /**<r0 used in reconstruction. may get updated in cn2 estimation*/
    double l0;         /**<l0 used in reconstruction. may get updated in cn2 estimation*/
    dmat *ht;          /**<height of the layers to do tomography.*/
    dmat *wt;          /**<weight of the layers to to tomography. may get updated in cn2 estimation*/
    dmat *os;          /**<over sampling of the layers.*/
    dmat *dx;          /**<sampling in meter of the layers*/

    loc_t *ploc;       /**<reconstructed pupil*/
    long ploc_nx;      /**<Size of pmap used to build ploc*/
    long ploc_ny;      /**<size of pmap used to build ploc*/

    loc_t **xloc;      /**<reconstructed atmosphere grid.*/
    map_t **xmap;      /**<The map of xloc (only if tomo.square is true)*/
    long *xloc_nx;     /**<size of xmap used to build xloc*/
    long *xloc_ny;     /**<size of xmap used to build xloc*/
    dcell *xmcc;       /**<used for tip/tilt removal from tomographic screens.*/

    loc_t **aloc;      /**<actuator grid*/
    map_t **amap;      /**<square grid of actuators*/
    long **aembed;      /**<index to embed phi on aloc to square geometry of aloc_nx*aloc_ny.*/
    loc_t **alocm;     /**<misregistered actuator grid for ray tracing*/
    icell *actfloat;   /**<floating actuators*/
    icell *actstuck;   /**<stuck actuators*/

    dcell *aimcc;      /**<used for tip/tilt removal from DM commands.*/
    dsp  *W0;          /**<ploc weighting for circle of diam aper.d*/
    dmat *W1;          /**<ploc weighting for circle of diam aper.d*/
    dmat *fitwt;       /**<fit weighting in each direction.*/
    spcell *L2;        /**<Laplacian square regularization.*/
    spcell *L2save;    /**<save old L2 to update the tomography matrix.*/
    INVPSD_T *invpsd;  /**<data to apply inverse of psf to opd on xloc*/
    FRACTAL_T *fractal;/**<data to apply fractal regularization on opd on xloc*/
    FDPCG_T *fdpcg;    /**<fdpcg preconditioner data.*/
    spcell *GWR;       /**<gradient of wfs for recon: gtilt or ztilt (on amp) depending on gtype_recon.*/
    spcell *GP;        /**<Gradient operator from HXW. GX=GP*H.*/
    spcell *HXW;       /**<Ray tracing operator from xloc to ploc for all WFS.*/
    spcell *HXWtomo;   /**<Like GXtomo*/
    spcell *GX;        /**<Gradient operator for all WFS from each layer of xloc*/
    spcell *GXtomo;    /**<GX for tomography. excluding NGS in split tomography*/
    spcell *GXhi;      /**<GX for high order WFS.*/
    spcell *GXlo;      /**<GX for low order WFs*/
    spcell *GXfocus;   /**<GX used for focus tracking.*/
    dcell *GXL;        /**<dense GX for low order WFS in MV split tomography.*/
    dcell *MVRngs;     /**<NGS recon for MV split tomography*/
    dcell *MVModes;    /**<MVST Modes (svd'ed)*/
    dcell *MVGM;       /**<NGS WFS gradient operator from MVST Modes.*/
    spcell *GA;        /**<actuator to wfs grad.*/
    spcell *GAlo;      /**<GA of low order WFS.*/
    spcell *GAhi;      /**<GA of high order WFS.*/
    spcell *HXF;       /**<ray tracing propagator from xloc to ploc for fitting directions.*/
    spcell *HA;        /**<ray tracing from aloc to ploc for fitting directions.*/
    dcell *TT;         /**<TT modes for LGS WFS*/
    dcell *PTT;        /**<pinv of TT for tt removal from LGS gradients*/
    dcell *DF;         /**<Differential focus modes for LGS wfs*/
    dcell *TTF;        /**<Concatenation of TT and DF*/
    dcell *PTTF;       /**<pinv of TTF*/
    spcell *ZZT;       /**<single point piston constraint in tomography.*/
    dcell *fitNW;      /**<null modes for DM fit.*/
    spcell *actslave;  /**<force slave actuators to have similar value to active neighbor ones.*/
    spcell *actinterp; /**<Interpolation operator for floating actuators. Slaving does not work well in CG. */
    double fitscl;     /**<strength of fitting FLM low rank terms (vectors)*/
    spcell *sanea;     /**<Measurement noise covairance, sanea^2 for each wfs in radian^2*/
    spcell *saneal;    /**<cholesky decomposition L of sanea^2 for each wfs to compute noise propagation*/
    spcell *saneai;    /**<inverse of sanea^2 in radian^-2 for each wfs*/
    dcell *ecnn;       /**<covairance of Hx*(E*Cnn*E^t)*Hx^t: noise propagation to science.*/
    dmat *neam;        /**<subaperture averaged nea for each wfs*/
    double neamhi;     /**<average of neam for high order wfs.*/
    MUV_T RR;          /**<tomography right hand side matrix, solve RL*x=RR*y*/
    MUV_T RL;          /**<tomography left hand side matrix*/
    MUV_T FR;          /**<DM fit right hand size matrix, solve FL*x=FR*y*/
    MUV_T FL;          /**<DM fit left hand size matrix*/
    MUV_T LR;          /**<least square reconstructor rhs*/
    MUV_T LL;          /**<least square reconstructor lhs. solve LL*x=LR*y*/
    MOAO_T *moao;      /**<for MOAO DM fitting*/
    //For focus tracking.
    dcell *RFlgs;      /**<focus reconstruction from each LGS grad*/
    dcell *RFngs;      /**<focus reconstruction from NGS grad.*/
    dcell *RFtomo;     /**<focus recon from reconstructed X.*/
    NGSMOD_T *ngsmod;  /**<ngs mod in ad hoc split tomography.*/
    CN2EST_T *cn2est;  /**<For Cn2 Estimation*/
    int lowfs_gtilt;   /**<=1 if any low order wfs use gtilt in recon/simu*/
    int npsr;          /**<number of reconstructor phase screens.*/
    int ndm;           /**<number of DMs;*/
    int has_ttr;       /**<whether there is any tip/tilt removed WFS*/
    int has_dfr;       /**<whether there is any differential focus removed WFS*/
    int nthread;       /**<number of threads in reconstruction.*/
    int warm_restart;  /**<Use warm restart.*/
    int cxx;           /**<records parms->tomo.cxx*/
}RECON_T;

/**
   contains internal data for a type II integrator
*/
typedef struct TYPEII_T{
    dcell *lead;       /**<Output of lead filter*/
    dcell *errlast;    /**<Record error in last time*/
    dcell *firstint;   /**<First integrator output*/
}TYPEII_T;
typedef struct SIM_SAVE_T{
    /*cellarrs to save telemetry data.*/
    cellarr** wfspsfout; /**<special file to save wfs psf history*/
    cellarr** ztiltout;  /**<special file to save zernike wfs tilt history*/
    //Evaluation directions PSF.
    cellarr*  evlpsfolmean;  /**<science field psf OL time average*/
    cellarr** evlpsfmean;    /**<science field psf CL time average*/
    cellarr** evlpsfhist;    /**<to save time history of science field psf*/
    cellarr** ecovxx;     /**<the time history of xx used to calculate ecov.*/
    //Deformable mirror.
    cellarr *dmerr_hi;
    cellarr *dmfit_hi;
    cellarr *dmpttr;
    cellarr *dmreal;
    cellarr *dmcmd;
    cellarr *dmproj;
    //Low order modes
    cellarr *Merr_lo;
    cellarr *opdr;
    cellarr *opdx;
    //science
    cellarr **evlopdcl;
    cellarr **evlopdol;

    cellarr **wfsopd;
    cellarr **wfsopdol;
    cellarr **wfslltopd;
    //gradients
    cellarr **gradcl;
    cellarr **gradgeom;
    cellarr **gradnf;
    cellarr **gradol;
    cellarr **intsny;
    cellarr **intsnf;
    cellarr **moao_evl;
    cellarr **moao_wfs;
    //covariances
    dcell *evlopdcov;
}SIM_SAVE_T;
/*
  data wrap for wfsints.
*/
typedef struct WFSINTS_T{
    dcell *ints;
    ccell *psfout;
    dcell *pistatout;
    const dmat *gradref;
    const dmat *opd;
    const dmat *lltopd;
    const PARMS_T *parms;
    const POWFS_T *powfs;
    int iwfs;
}WFSINTS_T;
/**
   contains data related to DM hysterisis modeling for all the common DMs (not
MOAO). let input be x, and output of each mode be y.  */
typedef struct{
    dmat *coeff;      /**<contains data from parms->dm.hyst*/
    dmat *xlast;      /**<Record x from last time step*/
    dmat *ylast;      /**<Record y from last time step*/
    dmat *dxlast;     /**<Change of x*/
    dmat *x0;         /**<Initial x (before dx changes sign)*/
    dmat *y0;         /**<Initial y*/
}HYST_T;
/**
   contains all the run time data struct.
*/
typedef struct SIM_T{
    /*A few data structs generated in beginning of simulation*/
    /* random stream. We maintain separate random streams for each purpose,
       derived from the same seed, so that multi-threading will produce same
       result */
    rand_t *wfs_rand;  /**<random stream for each wfs.*/
    rand_t *atm_rand;  /**<random stream for atmosphere turbulence generation*/
    rand_t *atmwd_rand;/**<random stream for wind direction*/
    rand_t *telws_rand;/**<random stream for wind shake*/
    rand_t *init;      /**<random stream to initialize other streams*/

    /*Atmosphere*/
    GENSCREEN_T *genscreen;
    map_t **atm;       /**<fine sampled simulation turbulence screens*/
    map_t **atm2;      /**<another fine sampled simulation turbulence screen for evolving.*/
    map_t ***cachedm;  /**<grid cache dm actuator to a finer sampled screen. for
			  fast ray tracing to WFS and aper*/
    int (*pcachedm)[2];/**<information about cachedm struct.*/
    dmat *winddir;     /**<input wind direction*/
    
    /*Optional surface errors in M1, M2, or M3*/
    rectmap_t **tsurf; /**<input tilted M3 surface read from parms->tsurf*/
    map_t **surf;      /**<input surface: M1, M2 or else. common to all wfs and science field.*/
    dcell *surfwfs;    /**<additional OPD surface for each WFS.*/
    dcell *surfevl;    /**<additional OPD surface for each evaluation field*/
    dcell *surfopdx;   /**<additional OPD surface projected onto xloc for fitonly*/

    /*Telescope windshake time series and direction.*/
    dmat  *telws;      /**<Telescope wind shake time series, along ground layer wind direction*/
    double telwsx;     /**<cos(ws_theta)*/
    double telwsy;     /**<sin(ws_theta)*/

    /*WFS data for each time step. Each has a cell for each wfs*/
    dcell **ints;      /**<WFS subaperture images.*/
    ccell **wfspsfout; /**<output WFS PSF history.*/
    dcell *wfspsfmean; /**<To save time average of wfs psf of the full aperture. (for PSF recon)*/
    dcell **pistatout; /**<WFS time averaged tip/tilt removed PSF*/
    dcell *gradcl;     /**<cl grad output at step isim.*/
    dcell **sanea_sim;  /**<accumulate effective sanea during simulation.*/
    dcell *gradacc;    /**<accumulate gradident for dtrat>1*/
    dcell *gradlastcl; /**<cl grad from last time step, for reconstructor*/
    dcell *gradlastol; /**<psol grad from last time step, for reconstructor*/

    /*Tomography*/
    dcell *opdr;       /**<reconstructed OPD defined on xloc in tomography output.*/
    dcell *opdrmvst;   /**<average of opdr for MVST*/
    dcell *opdx;       /**<Ray tracing from atmosphere to xloc directly. Fixme:
			  do layer by layer fitting instead?*/
    /*Only on split tomography*/
    dmat *clem;        /**<lgs/ngs mod error in split tomography*/
    dcell *clemp;      /**<lgs/ngs mod error per direction. only on-axis is computed.*/
    dmat *corrNGSm;    /**<Correction of NGS mode. (integrator output)*/
    dmat *cleNGSm;     /**<Close loop ngs mods in split tomogrpahy. */
    dcell *cleNGSmp;   /**<(M'*w*phi);*/
    dcell *res;        /**<warping of ole,cletomo,cle,clem for easy saving.*/
    dmat *gtypeII_lo;  /**<gain for type II array.*/

    /*DM commands.*/
    dcell *dmcmd;      /**<This is the final command send to DM.*/
    dcell *dmcmdlast;  /**<The final command for last time step.*/
    dcell *dmreal;     /**<This is the actual position of DM actuators after
			  receiving command dmcmd. Should only be used in
			  system, not in reconstruction since it is unknown.*/
    map_t **dmrealsq;   /**<dmreal embeded into an square map, zero padded.*/
    dcell *dmproj;     /**<only used when sim.wfsalias=1. The projection of atm
			  onto DM space directly.*/
    dcell **dmpsol;    /**<time averaged dm command (dtrat>1) for psol grad*/
    dcell *dmhist;     /**<histogram of dm commands. if dbg.dmhist is 1.*/
    dcell **dmint;     /**<dm integrator. (used of fuseint==1)*/
    HYST_T**hyst;      /**<Hysterisis computation stat*/

    /*High order*/
    dcell *dmfit_hi;   /**<direct high order fit output*/
    dcell *dmerr_hi;   /**<high order dm error signal.*/
    dcell **dmint_hi;  /**< integrator for high order (only if fuseint==0)*/

    /*Low order*/
    dcell *Merr_lo;    /**<split tomography NGS mode error signal.*/
    dcell *Merr_lo_keep;/**<Keep Merr_lo for PSF recon.*/
    dcell **Mint_lo;   /**<NGS integrator (only used if fuseint==0)*/
    TYPEII_T *MtypeII_lo;  /**<intermediate results for type II/lead filter*/  
    
    /*llt pointing loop*/
    dcell *upterr;     /**<uplink error*/
    dcell *upterrlast; /**<uplink error from last step*/
    dcell *uptreal;    /**<uplink real*/
    dcell **uptint;    /**<uplink integrator output.*/
    dcell *upterrs;    /**<mmaped file to store upterr history*/
    dcell *uptcmds;    /**<mmaped file to store uptcmd history*/

    /*focus tracking loop*/
    dcell *focuslpf;   /**<focus tracking low pass filter*/
    dcell *focusint;   /**<focus tracking integrator*/

    /*science evaluation*/
    dmat  *opdevlground;  /**<evaluation opd for ground layer turbulence to save ray tracing.*/
    dcell *evlpsfmean;    /**<science field psf time average*/
    dcell *evlpsfolmean;  /**<science field OL PSF time averging*/

    /*Optinal telemetry saving for PSF reconstruction.*/
    dcell *ecov;       /**<covariance of Hx*x-Ha*a for science directions.*/
    dcell *gcov;       /**<covariance of psuedo open loop gradients.*/
    /*save performance results to file using mmap*/
    dcell *clep;       /**<CL error per direction.*/
    dcell *clmp;       /**<CL mode coefficient per direction.*/
    dcell *olep;       /**<OL error per direction.*/
    dcell *olmp;       /**<OL mode coefficient per direction.*/
    dmat *ole;         /**<field averaged OL error*/
    dmat *cle;         /**<field averaged CL error*/

    /*MOAO*/
    dcell *moao_wfs;   /**<moao DM command computed for wfs*/
    dcell *moao_evl;   /**<moao DM command computed for science field*/

    double tk_eval;    /**<time spent in perfevl in this step*/
    double tk_recon;   /**<time spent in reconstruct in this step*/
    double tk_cache;   /**<time spent in cachedm in this step*/
    double tk_wfs;     /**<time spent in wfsgrad in this step*/

    /*A few data wraps for multi-threading*/
    PROPDATA_T *cachedm_propdata; /**<wrapped data for ray tracing from aloc to cachedm*/
    PROPDATA_T *wfs_propdata_dm;  /**<wrap of data for ray tracing from DM in wfsgrad.c*/
    PROPDATA_T *wfs_propdata_atm; /**<wrap of data for ray tracing from ATM in wfsgrad.c*/
    PROPDATA_T *evl_propdata_atm;
    PROPDATA_T *evl_propdata_dm;
    thread_t **cachedm_prop;   /**<wrapped cachedm_propdata for threading*/
    thread_t  **wfs_prop_dm;   /**<wrap of wfs_propdata_dm for threaded ray tracing*/
    thread_t  **wfs_prop_atm;  /**<wrap of wfs_propdata_atm for threaded ray tracing*/
    thread_t  **evl_prop_atm;
    thread_t  **evl_prop_dm;

    WFSINTS_T *wfs_intsdata;  /**<wrap of data for wfsints.c*/
    thread_t  **wfs_ints;     /**<wrap of wfs_intsdata for threaded processing*/

    thread_t  *wfs_grad; /**to call wfsgrad_iwfs in threads.*/
    thread_t  *perf_evl; /**to call perfevl_ievl in threads.*/

    SIM_SAVE_T *save;  /**<Telemetry output*/
    STATUS_T *status;  /**<status report to scheduler.*/

    /**For testing*/
    ccell *opdrhat;    /**<For wind estimation (testing)*/
    ccell *opdrhatlast;/**<for wind estimation.(testing)*/
    dmat *windest;     /**<estimated wind velocity.(testing)*/
    spcell *windshift; /**<operator to do wind shift on opdr(testing)*/

    /*A few indicators*/
    int nthread;       /**<number of threads*/
    int wfsints_isa;   /**<sa counter for wfsints*/
    int perfevl_iground;/**<index of the layer at ground*/
    int cachedm_n;     /**<length of pcachedm array*/
    int dtrat_hi;      /**<ratio of sampling period over clock of high order wfs*/
    int dtrat_lo;      /**<dtrat of the lower order loop.*/
    int seed;          /**<current running seed.*/
    int iseed;         /**<index of current running seed.*/
    int isim;          /**<record current simulations step.*/
    int reconisim;     /**<The time step for the gradlast data struct. =isim for OL, =isim-1 for CL*/
    /*maintain pointer of other structs for use in thread functions.*/
    const PARMS_T *parms; /**<pointer to parms*/
    const APER_T *aper;/**<pointer to aper*/
    RECON_T *recon;    /**<pointer to recon*/
    POWFS_T *powfs;    /**<pointer to powfs*/
    double dt;         /**<System baseline clock period. 1/800 s*/
    double dtlo;       /**<low order wfs sampling period*/
    double dthi;       /**<high order wfs sampling period*/
    int has_upt;       /**<whether we have uplink pointer loop.*/
    int last_report_time;/**<The time we lasted reported status to the scheduler.*/
}SIM_T;

#endif
