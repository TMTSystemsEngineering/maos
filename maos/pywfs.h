/*
  Copyright 2009-2022 Lianqi Wang <lianqiw-at-tmt-dot-org>
  
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


#ifndef AOS_PYWFS_H
#define AOS_PYWFS_H

/**
   Parameters used by Pyramid WFS
 */
typedef struct pywfs_t{
    real hs;         /**<Height of guide star*/
    real hc;         /**<Conjugation height of WFS pupil*/
    real poke;       /**<How much to poke for mkg*/
    real modulate;   /**<Amount of modulation in radian*/
    int modulpos;      /**<Number of positions per modulation cycle*/
    int modulring;     /**<Number of rings within the maximum radius to modulate*/
    int iwfs0;         /**<First iwfs for this powfs*/
    int nside;         /**<Number of sides*/
    int gpu;           /**<Whether GPU can be used*/
    dmat *wvlwts;      /**<parms->powfs.wvlwts*/
    loc_t *loc;        /**<Pupil plane grid*/
    dmat  *amp;        /**<Pupil plane amplitude map*/
    locfft_t *locfft;  /**<First fft to form PSF*/
    ccell *pyramid;    /**<OPD of pyramid. Angular size of clear aperture is different*/
    cmat *nominal;     /**<For sampling results onto detector*/
    dspcell *si;       /**<For sampling results onto detector*/
    dmat *sioff;       /**<Offset for si*/
    real gain;       /**<Optical gain of PYWFS*/
    dmat *saa;         /**<Subaperture area. Average is one*/
    dmat *gradoff;     /**<Gradient of a flat wavefront*/
    dmat *GTT;          /**<Response of TT mode.*/
    dmat *pupilshift;  /**<Pupil shift. 4x2.*/
    loccell *msaloc;   /**<Mishaped saloc of each sub-pupil due to optical effects*/
    int sigmatch;      /**<Scale gradients by matching intensity (1: locally, 2: globally).*/
    real siglev;     /**<Nominal siglev per subaperture*/
    dmat *opdadd;      /**<Aberrations along the WFS path (on locfft grid)*/
}pywfs_t;

void pywfs_setup(powfs_t *powfs, const parms_t *parms, aper_t *aper, int ipowfs);
void pywfs_free(pywfs_t *pywfs);
void pywfs_grad(dmat **pgrad, const pywfs_t *pywfs, const dmat *ints);
void pywfs_fft(dmat **ints, const pywfs_t *pywfs, const dmat *opd);
dmat* pywfs_mkg(pywfs_t *pywfs, const loc_t* ploc, const char *distortion, 
		const dmat *mod, const dmat *opdadd, real displacex,  real displacey);
dmat *pywfs_tt(const pywfs_t *pywfs);

#endif
