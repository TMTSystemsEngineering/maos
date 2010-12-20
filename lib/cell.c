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


#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <sys/mman.h>


#include "common.h"
#include "random.h"
#include "misc.h"
#include "dmat.h"
#include "cmat.h"
#include "matbin.h"
#include "dsp.h"
#include "csp.h"
#ifndef MAT_TYPE
#define MAT_TYPE
#ifndef USE_COMPLEX
#define X(A) d##A
#define T double
#define REAL(A) (A)
#define ABS(A) fabs(A)
#define RANDU(A) randu(A)
#define RANDN(A) randn(A)
#define PRINT(A) printf("%10.3e",A);
#else
#define X(A) c##A
#define T dcomplex
#define REAL(A) creal(A)
#define ABS(A) cabs(A)
#define RANDU(A) (randu(A)+I*randu(A))
#define RANDN(A) (randn(A)+I*randn(A))
#define PRINT(A) printf("(%10.3e %10.3eI)",creal(A),cimag(A));
#endif
#endif

#define PCELL(M,P) X(mat)* (*P)[(M)->nx]=(X(mat)*(*)[(M)->nx])(M)->p

/**
   create a new block matrix.
*/
X(cell) *X(cellnew)(const long nx, const long ny){
    X(cell) *dc;
    dc=calloc(1, sizeof(X(cell)));
    dc->nx=nx;
    dc->ny=ny;
    dc->p=calloc(nx*ny, sizeof(void*));
    return dc;
}

/**
   create an new X(cell) similar to A in shape
*/
X(cell) *X(cellnew2)(const X(cell) *A){
    X(cell) *out=X(cellnew)(A->nx, A->ny);
    for(int i=0; i<A->nx*A->ny; i++){
	if(A->p[i]){
	    out->p[i]=X(new)(A->p[i]->nx, A->p[i]->ny);
	}
    }
    return out;
}

/**
   Free a X(cell) object.
*/
void X(cellfree_do)(X(cell) *dc){
    if(!dc) return;
    if(dc->p){
	for(int ix=0; ix<dc->nx*dc->ny; ix++){
	    X(free)(dc->p[ix]);
	}
	if(dc->mmap){
	    munmap(dc->mmap, sizeof(long)*2+sizeof(uint32_t));
	}
	free(dc->p);
    }
    free(dc);
}

/**
   creat a X(cell) reference an existing X(cell) by referencing the
   elements.
*/
X(cell) *X(cellref)(const X(cell) *in){
    if(!in) return NULL;
    X(cell) *out=X(cellnew)(in->nx, in->ny);
    for(int i=0; i<in->nx*in->ny; i++){
	out->p[i]=X(ref)(in->p[i]);
    }
    return out;
}

/**
   duplicate a X(cell) object.
*/
X(cell) *X(celldup)(const X(cell) *in){
    X(cell) *out=NULL;
    X(cellcp)(&out, in);
    return out;
}

/**
   copy the values from one X(cell) to another.
*/
void X(cellcp)(X(cell)** out0, const X(cell) *in){
    if(in){
	if(!*out0)
	    *out0=X(cellnew)(in->nx, in->ny);
	X(cell)* out=*out0;
	assert(out->nx==in->nx && out->ny==in->ny);
	for(int i=0; i<in->nx*in->ny; i++){
	    X(cp)(&out->p[i], in->p[i]);
	}
    }else{//if input is empty, zero the output. do not free.
	X(cellzero)(*out0);
    }
}

/**
   setting all elements of a X(cell) to zero.
*/
void X(cellzero)(X(cell) *dc){
    if(dc){
	for(int ix=0; ix<dc->nx*dc->ny; ix++){
	    X(zero)(dc->p[ix]);
	}
    }
}

/**
   setting all elements of a X(cell) to alpha.
*/
void X(cellset)(X(cell)*dc, T alpha){
    if(dc){
	for(int ix=0; ix<dc->nx*dc->ny; ix++){
	    X(set)(dc->p[ix],alpha);
	}
    }
}

/**
   transpose a X(cell) object
*/
X(cell) *X(celltrans)(const X(cell) *A){
    if(!A) return NULL;
    X(cell) *B=X(cellnew)(A->ny, A->nx);
    X(mat)* (*Bp)[B->nx]=(void*)B->p;
    X(mat)* (*Ap)[A->nx]=(void*)A->p;
    for(int ix=0; ix<A->nx; ix++){
	for(int iy=0; iy<A->ny; iy++){
	    Bp[ix][iy]=X(trans)(Ap[iy][ix]);
	}
    }
    return B;
}

/**
   compute norm2.
*/
double X(cellnorm2)(const X(cell) *A){
    double out=0;
    for(int i=0; i<A->nx*A->ny; i++){
	out+=X(norm2)(A->p[i]);
    }
    return out;
}

/**
   scale each element of A.
*/
void X(cellscale)(X(cell) *A, double w){
    if(!A) return;
    for(int i=0; i<A->nx*A->ny; i++){
	X(scale)(A->p[i],w);
    }
}
/**
   Obtain the dimensions.
*/
static void X(celldim)(const X(cell) *A, long *nx, long *ny,
		       long **nxs, long **nys){
    X(mat) *(*Ap)[A->nx] = (X(mat) *(*)[A->nx])A->p;
    *nxs=calloc(A->nx, sizeof(long));
    *nys=calloc(A->ny, sizeof(long));
    *nx=0;
    *ny=0;
    for(long ix=0; ix<A->nx; ix++){
	for(long iy=0; iy<A->ny; iy++){
	    if(Ap[iy][ix]){
		*nx+=Ap[iy][ix]->nx;
		(*nxs)[ix]=Ap[iy][ix]->nx;
		break;
	    }
	}
    }
    for(long iy=0; iy<A->ny; iy++){
	for(long ix=0; ix<A->nx; ix++){
	    if(Ap[iy][ix]){
		*ny+=Ap[iy][ix]->ny;
		(*nys)[iy]=Ap[iy][ix]->ny;
		break;
	    }
	}
    }
}

/**
   reduce nx*ny cell matrix to 1*ny if dim=1 and nx*11 if dim=2
*/
X(cell) *X(cellreduce)(const X(cell)*A, int dim){
    if(!A) return NULL;
    X(cell)* out=NULL;
    long nx, ny, *nxs, *nys;
    X(celldim)(A, &nx, &ny, &nxs, &nys);
    if(nx==0 || ny==0) return NULL;
    PCELL(A,pA);
    if(dim==1){
	out=X(cellnew)(1, A->ny);
	for(long iy=0; iy<A->ny; iy++){
	    if(nys[iy]==0) continue;
	    out->p[iy]=X(new)(nx,nys[iy]);
	    for(long icol=0; icol<nys[iy]; icol++){
		long kr=0;
		for(long ix=0; ix<A->nx; ix++){
		    if(pA[iy][ix]){
			memcpy(out->p[iy]->p+icol*nx+kr,
			       pA[iy][ix]->p+icol*nxs[ix],
			       nxs[ix]*sizeof(T));
		    }			
		    kr+=nxs[ix];
		}
	    }
	}
    }else if(dim==2){
	out=X(cellnew)(A->nx,1);
	for(long ix=0; ix<A->nx; ix++){
	    if(nxs[ix]==0) continue;
	    out->p[ix]=X(new)(nxs[ix],ny);
	    long kr=0;
	    for(long iy=0; iy<A->ny; iy++){
		if(pA[iy][ix]){
		    memcpy(out->p[ix]->p+kr*nxs[ix],
			   pA[iy][ix]->p,
			   nxs[ix]*nys[iy]*sizeof(T));
		}
		kr+=nys[iy];
	    }
	}
    }else{
	error("Invalid dim=%d\n",dim);
    }
    free(nxs);
    free(nys);
    return out;
}

/**
   concatenate two cell matrices along dimenstion 'dim'
*/
X(cell) *X(cellcat)(const X(cell) *A, const X(cell) *B, int dim){
    if(!A){
	if(!B){
	    return NULL;
	}else{
	    return X(celldup)(B);
	}
    }else if(!B){
	return X(celldup)(A);
    }

    X(cell) *out=NULL;
    PCELL(A,pA);
    PCELL(B,pB);

    if(dim==1){
	//along x.
	if(A->ny!=B->ny){
	    error("Mismatch: A is (%ld, %ld), B is (%ld, %ld)\n",
		  A->nx, A->ny, B->nx, B->ny);
	}
	out=X(cellnew)(A->nx+B->nx, A->ny);
	PCELL(out,pout);
	for(long iy=0; iy<A->ny; iy++){
	    for(long ix=0; ix<A->nx; ix++){
		pout[iy][ix]=X(dup)(pA[iy][ix]);
	    }
	    for(long ix=0; ix<B->nx; ix++){
		pout[iy][ix+A->nx]=X(dup)(pB[iy][ix]);
	    }
	}
    }else if(dim==2){
	//along y.
	if(A->nx!=B->nx){
	    error("Mismatch. A is (%ld, %ld), B is (%ld, %ld)\n", 
		  A->nx, A->ny, B->nx, B->ny);
	}
	out=X(cellnew)(A->nx, A->ny+B->ny);
	PCELL(out,pout);
	for(long iy=0; iy<A->ny; iy++){
	    for(long ix=0; ix<A->nx; ix++){
		pout[iy][ix]=X(dup)(pA[iy][ix]);
	    }
	}
	for(long iy=0; iy<B->ny; iy++){
	    for(long ix=0; ix<B->nx; ix++){
		pout[iy+A->ny][ix]=X(dup)(pB[iy][ix]);
	    }
	}
    }else{
	error("Invalid dim\n");
    }
    return out;
}

/**
   concatenate coresponding elements of each X(cell). They must
   have the same shape.
*/
X(cell) *X(cellcat_each)(const X(cell) *A, const X(cell) *B, int dim){
    if(!A){
	if(!B){
	    return NULL;
	}else{
	    return X(celldup)(B);
	}
    }else if(!B){
	return X(celldup)(A);
    }
    if(A->nx!=B->nx || A->ny!=B->ny){
	error("Mismatch: (%ld %ld), (%ld %ld)\n",A->nx, A->ny, B->nx, B->ny);
    }
    X(cell) *out=X(cellnew)(A->nx, A->ny);
    for(long ix=0; ix<A->nx*A->ny; ix++){
	out->p[ix]=X(cat)(A->p[ix], B->p[ix], dim);
    }
    return out;
}

/**
   drop empty rows or columns. (size of *A0 is changed.
*/
void X(celldropempty)(X(cell) **A0, int dim){
    X(cell) *A=*A0;
    if(!A) return;
    PCELL(A,pA);
    if(dim==1){
	//drop rows
	int keep[A->nx];
	int ndrop=0;
	for(int ix=0; ix<A->nx; ix++){
	    keep[ix]=0;
	    for(int iy=0; iy<A->ny; iy++){
		if(pA[iy][ix]){
		    keep[ix]=1;
		    break;
		}
	    }
	    if(keep[ix]==0)
		ndrop++;
	}
	if(ndrop!=0){
	    if(ndrop==A->nx){
		X(cellfree)(A);
		*A0=NULL;
	    }else{
		X(cell) *B=calloc(1, sizeof(X(cell)*));
		B->p=calloc((A->nx-ndrop)*A->ny, sizeof(X(mat)*));
		PCELL(B,pB);
		int count=0;
		for(int ix=0; ix<A->nx; ix++){
		    if(keep[ix]){
			if(count!=ix){
			    for(int iy=0; iy<A->ny; iy++){
				pB[iy][count]=pA[iy][ix];
			    }
			}
			count++;
		    }else{
			warning("row %d dropped\n", ix);
		    }
		}
		free(A->p); free(A);
		A=B;
	    }
	}
    }else if(dim==2){
	//drop cols
	int count=0;
	for(int iy=0; iy<A->ny; iy++){
	    int keep=0;
	    for(int ix=0; ix<A->nx; ix++){
		if(pA[iy][ix]){
		    keep=1;
		    break;
		}
	    }
	    if(keep){
		for(int ix=0; ix<A->nx; ix++){
		    pA[count][ix]=pA[iy][ix];
		}
		count++;
	    }else{
		//warning("Col %d dropped\n", iy);
	    }
	}
	A->ny=count;
	if(count==0){
	    X(cellfree)(A);
	    *A0=NULL;
	}else{
	    A->p=realloc(A->p,sizeof(X(mat)*)*A->ny*A->nx);
	}
    }else{
	error("Invalid dim: %d\n",dim);
    }
 
}

/**
   add one to another.  B=B*bc+A*ac
*/
void X(celladd)(X(cell) **B0, double bc, const X(cell) *A,const double ac){
    if(A){
	if(!*B0){
	    *B0=X(cellnew)(A->nx, A->ny); bc=0;
	}
	X(cell) *B=*B0;
	assert(A->nx==B->nx && A->ny == B->ny);
	for(int i=0; i<A->nx*A->ny; i++){
	    X(add)(&B->p[i], bc, A->p[i], ac);
	}
    }
}

/**
   Compute the inner produce of two dcell.
*/
T X(cellinn)(const X(cell)*A, const X(cell)*B){
    if(A->nx!=B->nx || A->ny!=1 || B->ny!=1) error("mismatch\n");
    T out=0;
    for(int i=0; i<A->nx; i++){
	out+=X(inn)(A->p[i], B->p[i]);
    }
    return out;
}

/**
   Component wise multiply of two dcell
   B=A.*B*alpha
*/
void X(cellcwm)(X(cell) *B, const X(cell) *A){
    if(A->nx!=B->nx || A->ny !=B->ny) error("mismatch\n");
    for(int i=0; i<A->nx*A->ny; i++){
	X(cwm)(B->p[i], A->p[i]);
    }
}

/**
   Compute A*B and add to C0.
   C=C+trans(A)*trans(B)*alpha
       
   2009-11-09: There was initially a beta parameter
   It was implemented wrongly for beta!=1 because for every 
   call to dmm, the already accumulated ones are scaled.
   removed beta.
*/
void X(cellmm)(X(cell) **C0, const X(cell) *A, const X(cell) *B, 
	       const char trans[2], const double alpha){
    if(!A || !B) return;
    int ax, az;
    int nx,ny,nz;
    int bz, by;
    if(trans[0]=='n'||trans[0]=='N'){
	nx=A->nx; 
	ax=1; az=A->nx;
	nz=A->ny;
    }else{ 
	nx=A->ny;
	az=1; ax=A->nx;
	nz=A->nx;
    }
    if(trans[1]=='n'||trans[0]=='N'){
	ny=B->ny; 
	bz=1; by=B->nx;
	if(nz!=B->nx) error("mismatch\n");
    }else{
	ny=B->nx;
	by=1; bz=B->nx;
	if(nz!=B->ny) error("mismatch\n");
    }
    if(!*C0){
	*C0=X(cellnew)(nx,ny);
    }
    X(cell) *C=*C0;
    for(int iy=0; iy<ny; iy++){
	for(int ix=0; ix<nx; ix++){
	    for(int iz=0; iz<nz; iz++){
		if(A->p[ix*ax+iz*az]&&B->p[iz*bz+iy*by]){
		    X(mm)(&C->p[ix+iy*nx],A->p[ix*ax+iz*az], 
			  B->p[iz*bz+iy*by],trans,alpha);
		}
	    }
	}
    }
}

/**
   Inplace Invert a SPD matrix. It is treated as a block matrix
*/
X(cell)* X(cellinvspd)(X(cell) *A){
    X(mat) *Ab=X(cell2m)(A);
    X(invspd_inplace)(Ab);
    X(cell) *B=NULL;
    X(2cell)(&B, Ab, A);
    X(celldropzero)(B,0);
    X(free)(Ab);
    return B;
}

/**
   Inplace Invert a matrix. It is treated as a block matrix.
*/
X(cell)* X(cellinv)(X(cell) *A){
    X(mat) *Ab=X(cell2m)(A);
    X(inv_inplace)(Ab);
    X(cell) *B=NULL;
    X(2cell)(&B, Ab, A);
    X(celldropzero)(B,0);
    X(free)(Ab);
    return B;
}
/**
   invert each component of the dcell. Each cell is treated
   as an individual matrix.
*/
X(cell)* X(cellinvspd_each)(X(cell) *A){
    X(cell) *out=NULL;
    X(cellcp)(&out,A);
    for(int i=0; i<out->nx*out->ny; i++){
	X(invspd_inplace)(out->p[i]);
    }
    return out;
}

/**
   Convert a block matrix to a matrix.
*/
X(mat) *X(cell2m)(const X(cell) *A){
    X(mat) *(*Ap)[A->nx] = (X(mat) *(*)[A->nx])A->p;
    long nx,ny,*nxs,*nys;
    X(celldim)(A,&nx,&ny,&nxs,&nys);
    X(mat) *out=X(new)(nx,ny);
    long jcol=0;
    for(long iy=0; iy<A->ny; iy++){
	for(long icol=0; icol<nys[iy]; icol++){
	    long kr=0;
	    for(long ix=0; ix<A->nx; ix++){
		if(Ap[iy][ix]){
		    memcpy(out->p+((icol+jcol)*nx+kr),
			   Ap[iy][ix]->p+icol*nxs[ix],
			   nxs[ix]*sizeof(T));
		}
		kr+=nxs[ix];
	    }
	}
	jcol+=nys[iy];
    }
    free(nxs);
    free(nys);
    return out;
}

/**
   convert a vector to cell using dimensions specified in dims. Reference the vector
*/
X(cell)* X(2cellref)(const X(mat) *A, int*dims, int ndim){
    long nx=0;
    for(int ix=0; ix<ndim; ix++){
	nx+=dims[ix];
    }
    if(A->ny!=1){
	error("Use d2cell2 instead for non vectors\n");
    }
    if(nx!=A->nx ){
	error("Shape doesn't agree. nx=%ld, nx=%ld\n", nx,A->nx);
    }
    long kr=0;
    X(cell) *B=X(cellnew)(ndim,1);
    for(long ix=0; ix<ndim; ix++){
	B->p[ix]=X(new_ref)(A->p+kr,dims[ix],1);//refrence the data. 
	kr+=dims[ix];
    }
    return B;
}

/**
   make A a cell array using shape information from ref if *B is NULL
*/
void X(2cell)(X(cell) **B, const X(mat) *A, const X(cell) *ref){
    long nx,ny,*nxs,*nys;
    if(*B) ref=*B;//use B as reference.
    X(celldim)(ref, &nx, &ny, &nxs, &nys);
    if(nx!=A->nx || ny!=A->ny){
	error("Shape doesn't agree. Reference is %ldx%ld but input is %ldx%ld\n",
	      nx,ny,A->nx,A->ny);
    }
    if(!*B){
	*B=X(cellnew)(ref->nx, ref->ny);
	PCELL((*B),Bp);
	for(long iy=0; iy<ref->ny; iy++){
	    for(long ix=0; ix<ref->nx; ix++){
		Bp[iy][ix]=X(new)(nxs[ix],nys[iy]);
	    }
	}
    }
    PCELL((*B),Bp);
    long jcol=0;
    for(long iy=0; iy<ref->ny; iy++){
	for(long icol=0; icol<nys[iy]; icol++){
	    long kr=0;
	    for(long ix=0; ix<ref->nx; ix++){
		if(nxs[ix]>0){
		    memcpy(Bp[iy][ix]->p+icol*Bp[iy][ix]->nx,
			   A->p+((icol+jcol)*nx+kr),
			   nxs[ix]*sizeof(T));
		    kr+=nxs[ix];
		}
	    }
	}
	jcol+=nys[iy];
    }
    free(nxs);
    free(nys);
}

/**
   drop empty blocks (zero). Size of B is not modified.
*/
void X(celldropzero)(X(cell) *B, double thres){
    PCELL(B,Bp);
    for(long iy=0; iy<B->ny; iy++){
	for(long ix=0; ix<B->nx; ix++){
	    X(mat) *tmp=Bp[iy][ix];
	    if(!tmp) continue;
	    int hasnonzero=0;
	    for(int ixy=0; ixy<tmp->nx*tmp->ny; ixy++){
		if(ABS(tmp->p[ixy])>thres){
		    hasnonzero=1;
		    break;
		}
	    }
	    if(!hasnonzero){
		X(free)(Bp[iy][ix]);
		Bp[iy][ix]=NULL;
		//warning("Dropped block (%ld, %ld)\n", ix, iy);
	    }
	}
    }
}

/**
   compute ||A-B||/||A||
   use mean.
*/
double X(celldiff)(const X(cell) *A, const X(cell) *B){
    X(cell) *C=NULL;
    X(cellcp)(&C,A);
    X(celladd)(&C,1,B,-1);
    double d=sqrt(X(cellnorm2)(C)*2/(X(cellnorm2)(C)+X(cellnorm2)(B)));
    return isnan(d)?0:d;
}

/**
   clip a X(cell) array to max at 'max', min at 'min'
*/
int X(cellclip)(X(cell) *Ac, double min, double max){
    if(!Ac || !Ac->p) return 0;
    if(isinf(min)==-1 && isinf(max)==1) return 0;
    int nclip=0;
    for(long i=0; i<Ac->nx*Ac->ny; i++){
	nclip+=X(clip)(Ac->p[i],min,max);
    }
    return nclip;
}

/**
   Apply tickholov regularization of relative thres to cell array by
   converting it to mat
*/
void X(celltikcr)(X(cell) *A, double thres){
    X(mat) *Ab=X(cell2m)(A);
    X(tikcr)(Ab,thres);
    X(2cell)(&A,Ab,NULL);
    X(free)(Ab);
}

/**
   compute the pseudo inverse of block matrix A.  A is n*p cell, wt n*n cell or
   sparse cell.  \f$B=inv(A'*W*A)*A'*W\f$  */
X(cell)* X(cellpinv)(const X(cell) *A,    /**<[in] The matrix to pseudo invert*/
		     const X(cell) *wt,   /**<[in] Use a dense matrix for weighting*/
		     const Y(spcell) *Wsp /**<[in] Use a sparse matrix for weighting*/
		     ){
    if(!A) return NULL;
    X(cell) *wA=NULL;
    if(wt){
	if(Wsp){
	    error("Both wt and Wsp are specified.\n");
	}
	assert(wt->nx==A->nx && wt->ny==A->nx);
	X(cellmm)(&wA, wt, A, "nn",1);
    }else{
	if(Wsp){
	    assert(Wsp->nx==A->nx && Wsp->ny==A->nx);
	    Y(spcellmulmat)(&wA,Wsp,A,1);
	}else{
	    wA=X(cellref)(A);
	}
    }

    X(cell) *ata=NULL;
    X(cell) *iata;
    X(cellmm)(&ata,wA,A,"tn",1);
    iata=X(cellinvspd)(ata);
    X(cellfree)(ata);
    X(cell) *out=NULL;
    X(cellmm)(&out, iata, wA, "nt",1);
    X(cellfree)(wA);
    X(cellfree)(iata);
    return out;
}

/**
   Multiply a cell with a sparse cell.

  \f$C0+=A*B*alpha\f$.
*/
void X(cellmulsp)(X(cell) **C0, const X(cell) *A, const Y(spcell) *B, double alpha){
    if(!A || !B) return;
    int ax, az;
    int nx,ny,nz;
    int bz, by;
    const char trans[2]="nn";
    if(trans[0]=='n'||trans[0]=='N'){
	nx=A->nx; 
	ax=1; az=A->nx;
	nz=A->ny;
    }else{ 
	nx=A->ny;
	az=1; ax=A->nx;
	nz=A->nx;
    }
    if(trans[1]=='n'||trans[0]=='N'){
	ny=B->ny; 
	bz=1; by=B->nx;
	if(nz!=B->nx) error("mismatch\n");
    }else{
	ny=B->nx;
	by=1; bz=B->nx;
	if(nz!=B->ny) error("mismatch\n");
    }
    if(!*C0){
	*C0=X(cellnew)(nx,ny);
    }
    X(cell) *C=*C0;
    for(int iy=0; iy<ny; iy++){
	for(int ix=0; ix<nx; ix++){
	    for(int iz=0; iz<nz; iz++){
		if(A->p[ix*ax+iz*az] && B->p[iz*bz+iy*by]){
		    X(mulsp)(&C->p[ix+iy*nx],A->p[ix*ax+iz*az], 
			     B->p[iz*bz+iy*by],alpha);
		}
	    }
	}
    }
}

/**
   add a to diagonal elements of A;
*/
void X(celladdI)(X(cell) *A, double a){
    if(A->nx!=A->ny) 
	error("A must be symmetric\n");
    for(int ib=0; ib<A->nx; ib++){
	X(addI)(A->p[ib+ib*A->nx], a);
    }
}

/**
   compute the power of a block matrix using svd method. First convert it do
   X(mat), do the power, and convert back to block matrix.
*/
void X(cellsvd_pow)(X(cell) *A, double power){
    X(mat) *Ac=X(cell2m)(A);
    X(svd_pow)(Ac, power);
    X(2cell)(&A, Ac, NULL);
    X(free)(Ac);
}

/**
   raise each cell in the cell array to power of power.
*/
void X(cellcwpow)(X(cell)*A, double power){
    for(long ib=0; ib<A->nx*A->ny; ib++){
	X(cwpow)(A->p[ib],power);
    }
}
/**
   Create a new sub cell matrix of nx*ny starting from(sx,sy)
*/
X(cell) *X(cellsub)(const X(cell) *in, long sx, long nx, long sy, long ny){
    if(nx<=0){
	nx=in->nx-sx;
    }
    if(ny<=0){
	ny=in->ny-sy;
    }
    X(cell)*out=X(cellnew)(nx, ny);
    if(sx+nx>in->nx || sy+ny>in->ny){
	error("Invalid parameter range\n");
    }
    PCELL(in, pin);
    PCELL(out, pout);
    for(int iy=0; iy<ny; iy++){
	for(int ix=0; ix<nx; ix++){
	    pout[iy][ix]=pin[iy+sy][ix+sx];
	}
    }
    return out;
}
