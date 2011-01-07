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

#ifndef AOS_LOC_H
#define AOS_LOC_H
#include "type.h"
#include "dmat.h"
#include "cmat.h"
#include "dsp.h"


int *loc_create_embed(int *nembed, const loc_t *loc);
void loc_create_map_npad(loc_t *loc, int npad);
void loc_create_map(loc_t *loc);

loc_t * map2loc(double dx, long nx, long ny, 
		double ox, double oy, double *map);
long *map2embed(long nx, long ny, double *map);
loc_t * sqmap2loc(map_t *amp);
long* sqmap2embed(map_t *amp);
void rectmapfree_do(rectmap_t *map);
#define rectmapfree(A) ({rectmapfree_do(A);A=NULL;})
void sqmapfree_do(map_t *map);
#define sqmapfree(A) ({sqmapfree_do(A);A=NULL;})
void sqmaparrfree_do(map_t **map, int nmap);
#define sqmaparrfree(A,B) ({sqmaparrfree_do(A,B);A=NULL;})
void loc_free_map(loc_t *loc);

void locfree_do(loc_t *loc);
#define locfree(A) ({locfree_do(A);A=NULL;})
void ptsfree_do(pts_t *pts);
#define ptsfree(A) ({ptsfree_do(A);A=NULL;})
void locarrfree_do(loc_t **loc, int nloc);
#define locarrfree(A,B) ({locarrfree_do(A,B);A=NULL;})

int loccenter(loc_t *loc);
loc_t *locnew(long nloc);
void loc_calc_ptt(double *out, double *coeffout, 
		  const loc_t *loc, const double ipcc, 
	       const dmat *imcc, const double *amp, const double *opd);
void loc_calc_mod(double *out, double *coeffout, 
		 const dmat *mod, const double *amp, double *opd);
dmat *loc_mcc_ptt(const loc_t *loc, const double *amp);
dcell *pts_mcc_ptt(const pts_t *pts, const double *amp);
void loc_remove_ptt(double *opd, const double *ptt, const loc_t *loc);
void loc_add_ptt(double *opd, const double *ptt, const loc_t *loc);
void pts_ztilt(double *out, const pts_t *pts, const dcell *imcc,
	       const double *amp, const double *opd);
loc_t *mk1dloc_vec(double *x, long nx);
loc_t *mk1dloc(double x0, double dx, long nx);
loc_t *mksqloc_auto(long nx, long ny, double dx);
loc_t *mksqloc_map(map_t*map);
loc_t *mksqloc(long nx, long ny, double dx, double ox, double oy);
loc_t *mksqlocrot(long nx, long ny, double dx, 
		  double ox, double oy, double theta);
loc_t *mkcirloc(double d, double dx);
loc_t *mkcirloc_amp(double** restrict ampout, locstat_t *cstat, 
		    map_t* ampin, double dtel, double dx, int cropamp);
void loc_create_stat_do(loc_t *loc);
#define loc_create_stat(loc) if(!loc->stat) loc_create_stat_do(loc);
void loc_free_stat(loc_t *loc);
void loccircle(double *phi,loc_t *loc,double cx,double cy,double r,double val);
void locellipse(double *phi,loc_t *loc,double cx,double cy,
		double rx,double ry,double val);
void loc_reduce_spcell(loc_t *loc, spcell *sp, int dim, int cont);
void loc_reduce_sp(loc_t *loc, dsp *sp, int dim, int cont);

dmat* loc_zernike(loc_t *loc, double R, int nr);
void loc_add_focus(double *opd, loc_t *loc, double val);
dmat *loc2mat(loc_t *loc,int piston);
loc_t *pts2loc(pts_t *pts);
void locrot(loc_t *loc, const double theta);
loc_t *locdup(loc_t *loc);
loc_t *loctransform(loc_t *loc, dmat **coeff);
void loc_nxny(long *nx, long *ny, const loc_t *loc);
map_t *mapnew(long nx, long ny, double dx, double *p);
void mapcircle(map_t *map, double r, double val);
#endif
