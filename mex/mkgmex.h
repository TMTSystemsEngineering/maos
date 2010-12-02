#ifndef __MKGMEX_H
#define __MKGMEX_H
#include "signal.h"
typedef long spint;
typedef struct dsp{
    long nzmax ; /* maximum number of entries */
    long m ;	    /* number of rows */
    long n ;	    /* number of columns */
    long *p ;    /* column pointers (size n+1) or col indlces (size nzmax) */
    long *i ;    /* row indices, size nzmax */
    double *x ;	    /* numerical values, size nzmax */
    long nz ;    /* # of entries in triplet matrix, -1 for compressed-col */
    long shmkey; /*an additional component that cs_dl doesn't have.*/
}dsp;
typedef struct locmap_t{
    struct loc_t *loc;
    long *p;
    double ox;
    double oy;
    int nx,ny;
    int npad;  
}locmap_t;
typedef struct loc_t{
    double *locx;/*points to x coordinates of each point*/
    double *locy;
    double dx; 
    long   nloc;/*number of points*/
    locmap_t *map;/*point to the map used for accphi only.*/
}loc_t;
void maxmin(double *p, int N, double *max, double *min);
void loc_free_map(loc_t *loc);
void loc_create_map(loc_t *loc);
dsp * mkgt(loc_t* xloc, loc_t *ploc, double *amp, loc_t *saloc, 
	      int saorc, double scale, double *displace, int do_partial);
dsp * spnew(long nx, long ny, long nzmax);
dsp *spcat(const dsp *A, const dsp *B, int type);
void spsetnzmax(dsp *sp, long nzmax);
void spfree(dsp *sp);
#define error(A) {fprintf(stderr,A); raise(SIGABRT);};
#define SPLIT(A,B,C) {C=ifloor(A); B=A-C;}
#define iceil(A) (int)ceil(A)
#define ifloor(A) (A<0?(int)(A)-1:(int)(A))
#endif