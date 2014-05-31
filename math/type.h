/*
  Copyright 2009-2013 Lianqi Wang <lianqiw@gmail.com> <lianqiw@tmt.org>
  
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

#ifndef AOS_MATH_TYPE_H
#define AOS_MATH_TYPE_H
#include "../sys/sys.h"
/**
   \file type.h Defines the math data types like dmat, cmat, dcell, ccell,
   dsp, csp data types.

   Don't use ulong for dimensions because subtracting a bigger ulong from a
   smaller ulong overflows.  */


/*
  Separate definition of struct with typedef. Put definition of struct in a
  private file and typedef in a public interface to hide the struct from the
  user */
typedef enum{
    M_DMAT=1,
    M_SMAT,
    M_CMAT,
    M_ZMAT,
    M_DSP,
    M_SSP,
    M_CSP,
    M_ZSP,
    M_CELL,
}M_TYPE;

typedef enum CEMBED{
    C_FULL,
    C_ABS2,
    C_REAL,
    C_ABS,
    C_LITERAL
}CEMBED;

/*
  We use pointers for reference counter because different array
  may use the same pointer, but with different nx or ny
  partition. */

#define ARR(T)						\
    long id;       /**< to identify the array type*/	\
    T *restrict p; /**<The data pointer*/		\
    long nx;       /**< number of rows */		\
    long ny;       /**< number of columns */		\
    char *header;  /**<The header*/			\
    struct mmap_t *mmap;/**< not NULL if mmaped.*/	\
    int *nref; /**< reference count */			\
    struct fft_t *fft					

#define MAT(T) struct{ \
	ARR(T);	       \
    }

#define CELL(T) struct{		      \
	ARR(T);			      \
	T m;/*store continuous data*/ \
    }

#define SPMAT(T) struct{						\
	long id;/**<to identify the array type*/			\
	T *restrict x;       /**< numerical values, size nzmax */	\
	union{long m;long nx;};	          /**< number of rows */	\
	union{long n;long ny;};	          /**< number of columns */	\
	char *header;         /**<header*/				\
	long nzmax ;          /**< maximum number of entries */		\
        spint *restrict p ;   /**< column pointers (size n+1) or col indices (size nzmax) when nz!=-1 */ \
	spint *restrict i ;   /**< row indices, size nzmax */		\
	long nz ;             /**< number of entries in triplet matrix, -1 for compressed-col */ \
	int *nref;           /**< reference counting like dmat */	\
    }

typedef MAT(double) dmat;/*a double matrix object contains 2-d array of double numbers*/
typedef MAT(float) smat;
typedef MAT(dcomplex) cmat;
typedef MAT(fcomplex) zmat;

typedef SPMAT(double) dsp;
typedef SPMAT(float) ssp;
typedef SPMAT(dcomplex) csp;
typedef SPMAT(fcomplex) zsp;

typedef CELL(cmat*) ccell;
typedef CELL(zmat*) zcell;
typedef CELL(dmat*) dcell;
typedef CELL(smat*) scell;

typedef CELL(dsp*) spcell;
typedef CELL(ssp*) sspcell;
typedef CELL(csp*) cspcell;
typedef CELL(zsp*) zspcell;

typedef CELL(void*) cell;


#define AOS_CMAT(A) c##A
#define AOS_CSP(A)  c##A
#define AOS_DMAT(A) d##A
#define AOS_DSP(A)  A
#define AOS_SSP(A)  s##A
#define AOS_SMAT(A) s##A
#define AOS_ZMAT(A) z##A
#define AOS_ZSP(A)  z##A
#endif
