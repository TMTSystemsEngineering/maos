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

#ifndef AOS_LIB_MATHMISC_H
#define AOS_LIB_MATHMISC_H

#include "common.h"
#include "blas.h"
void normalize(double *p, long nloc, double norm);
void normalize_max(double *p, long nloc, double max);
double dotdbl(const double *restrict p1, const double *restrict p2, 
	      const double *restrict p3, long n);
dcomplex dotcmp(const dcomplex *restrict p1, const dcomplex *restrict p2, 
		const double *restrict p3, long n);
double maxdbl(const double *p, long n);
double maxabs(const double *p, long n);

double dblsum(double *p, long nx);
#define sinc(x) (fabs(x)<1.e-5?1:sin(M_PI*x)/(M_PI*x))

void adddbl(double *restrict out, double alpha, 
	    const double *in, int N, double beta);
void invsq(long n, double *restrict A);
#define mysqrt(A) (A<0?-sqrt(-A):sqrt(A))
long *invperm(long *p, long np);
void cvecperm(dcomplex *restrict out, const dcomplex *restrict in, const long *perm, long nx);
void cvecpermi(dcomplex *restrict out, const dcomplex *restrict in, const long *perm, long nx);
void maxmindbl(const double *restrict p, long N, 
	       double *restrict max, double *restrict min);
void maxminlong(const long *restrict p, long N,
		long *restrict max, long *restrict min);
void maxmincmp(const dcomplex *restrict p, long N,
	       double *restrict max, double *restrict min, double *restrict sum);
#endif