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
#include "pcg.h"

/**
   solve A*x=b using (preconditioned) conjugate gradient method

   ; input x is first guess if not NULL and warm==1; A is symmetric
   positive definite (SPD) matrix plus low rand terms.  x0 contains the initial
   guess. Amul is the operator that applies A to x.  */
real pcg(dcell** px,    /**<[in,out] The output vector. input for warm restart.*/
	cgfun_t Amul,    /**<[in] The function that applies y=y+A*x*alpha: (*Amul)(&y,A,x,alpha)*/
	const void* A, /**<[in] Contain info about the The left hand side matrix A*/
	prefun_t Mmul,   /**<[in] The precond func that applies y=M^-1*x;*/
	const void* M, /**<[in] Contains info about preconditioner matrix M.*/
	const dcell* b,/**<[in] The right hand side vector to solve*/
	int warm,      /**<[in] Use warm restart (use the value contained in px)*/
	int maxiter    /**<[in] Max number of iterations*/
){

	dcell* r0=NULL, * x0=NULL, * z0=NULL;
	if(!*px){
		*px=dcellnew2(b);
	} else if(!warm){
		dcellzero(*px);
	}
	x0=*px;
	real r0z1=0, r0z2;
	dcell* p0=NULL;
	dcell* Ap=NULL;
	real ak, bk;
	real r0z0=dcelldot(b, b);/*|b| */
	real res[maxiter+1];
	res[maxiter]=0;
	dbg2("CPU %sCG %d:", M?"P":"", maxiter);

	for(int k=0; k<maxiter; k++){
		if(k%100==0){ /*restart every 100 steps exclude beginning */
			/*computes r0=b-A*x0 */
			dcellcp(&r0, b);/*r0=b; */
			(*Amul)(&r0, A, x0, -1);/*r0=r0+(-1)*A*x0 */
			if(Mmul){
				(*Mmul)(&z0, M, r0);
			} else{
				z0=r0;
			}
			dcellcp(&p0, z0);
			r0z1=dcelldot(r0, z0);
		}
		if(LOG_LEVEL>1){
			if(Mmul){
				res[k]=sqrt(dcelldot(r0, r0)/r0z0);
			} else{
				res[k]=sqrt(r0z1/r0z0);
			}
		}

		dbg2(" %.5f", res[k]);

		if(Ap) dcellzero(Ap);
		(*Amul)(&Ap, A, p0, 1);
		ak=r0z1/dcelldot(p0, Ap);
		if(isinf(ak)){
			ak=0;
			goto end;
		}
		dcelladd(&x0, 1, p0, ak);/*x0=x0+ak*p0 */
		dcelladd(&r0, 1, Ap, -ak);/*r0=r0-ak*Ap */
		if(k+1==maxiter){//final residual
			res[k+1]=sqrt(dcelldot(r0, r0)/r0z0);
			break;
		}
		if(Mmul){
			(*Mmul)(&z0, M, r0);
		} else{
			z0=r0;
		}
		r0z2=dcelldot(r0, z0);/*r0z2=r0'*r0 */
		bk=r0z2/r0z1;
		dcelladd(&p0, bk, z0, 1.);/*p0=bk*pi+r0 */
		r0z1=r0z2;
	}
	dbg2(" ==> %.5f\n", res[maxiter]);
end:
	dcellfree(r0);
	if(Mmul){
		dcellfree(z0);
	}
	dcellfree(Ap);
	dcellfree(p0);
	return res[maxiter];
}
