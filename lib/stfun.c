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
#include "../math/mathdef.h"
#include "stfun.h"

/**
   Data struct for structure function computation from digital data.
*/
struct stfun_t{
	long count;
	dmat* amp;
	cmat* hat0;//FFT of a
	cmat* hat1;//FFT of f*a
	cmat* hat2;//FFT of f*f*a
	cmat* hattot;
};

/**
   Initialize the stfun data to calculate structure function.
*/
stfun_t* stfun_init(long nx, long ny, real* amp){
	stfun_t* A=mycalloc(1, struct stfun_t);
	A->count=0;
	A->hat0=cnew(nx*2, ny*2);
	A->hat1=cnew(nx*2, ny*2);
	A->hat2=cnew(nx*2, ny*2);
	A->hattot=cnew(nx*2, ny*2);

	A->amp=dnew(nx, ny);
	if(amp){
		memcpy(P(A->amp), amp, nx*ny*sizeof(real));
	} else{
		dset(A->amp, 1);
	}
	cembedd(A->hat0, A->amp, 0);
	cfft2(A->hat0, -1);
	return A;
}
/**
   Accumulate OPDs.
 */
void stfun_push(stfun_t* A, dmat* opd){
	A->count++;
	const long nx=NX(A->amp);
	const long ny=NY(A->amp);
	const long nx2=(NX(A->hat0)-nx)>>1;//skip
	const long ny2=(NY(A->hat0)-ny)>>1;
	cset(A->hat1, 0);
	cset(A->hat2, 0);
	for(long iy=0; iy<ny; iy++){
		for(long ix=0; ix<nx; ix++){
			real o=P(opd, ix, iy)*P(A->amp, ix, iy);
			P(A->hat1, ix+nx2, iy+ny2)=o;
			P(A->hat2, ix+nx2, iy+ny2)=o*P(opd, ix, iy);
		}
	}
	cfft2(A->hat1, -1);
	cfft2(A->hat2, -1);
	for(long i=0; i<PN(A->hat1); i++){
		/*real(t2*t0^*)-t1*t1^* */
		P(A->hattot,i)+=creal(P(A->hat2,i)*conj(P(A->hat0,i)))
			-P(A->hat1,i)*conj(P(A->hat1,i));
	}
} 
/**
   Use accumulated data to compute the structure function.
 */
dmat* stfun_finalize(stfun_t* A){
	cscale(A->hattot, 2./A->count);//2 was omitted in accumulation
	cabs2toreal(A->hat0, 1); //hat0 * hat0^*
	cfft2i(A->hattot, 1);	
	cfft2i(A->hat0, 1);
	cfftshift(A->hattot);//fftshift is necessary
	cfftshift(A->hat0);
	const long nx=NX(A->hat0);
	const long ny=NY(A->hat0);
	dmat* st=dnew(nx, ny);
	for(long iy=1; iy<ny; iy++){/*skip first row/column where hat0 is 0. */
		for(long ix=1; ix<nx; ix++){
			const double overlap=creal(P(A->hat0, ix, iy));
			if(overlap>0.5){
				P(st, ix, iy)=creal(P(A->hattot, ix, iy))/overlap;
			}
		}
	}
	cfree(A->hat0);
	cfree(A->hat1);
	cfree(A->hat2);
	cfree(A->hattot);
	dfree(A->amp);
	free(A);
	return st;
}
/**
 * Batch process cell array of OPDs. 
 * */
dmat *stfun_batch(const_anyarray opd_, /**<nz array of nx*ny array of the OPD*/
	const dmat *amp /**<n*ny array of the amplitude map*/
){
	const cell *opd=opd_.c;
	if(!opd){
		error("OPD should be provided\n");
	}
	stfun_t *A=NULL;
	if(iscell(opd)){
		A=stfun_init(NX(P(opd,0)), NY(P(opd,0)), amp?P(amp):NULL);
		for(long i=0; i<PN(opd); i++){
			stfun_push(A, dmat_cast(P(opd, i)));
		}
	}else{
		A=stfun_init(NX(opd), NY(opd), amp?P(amp):NULL);
		stfun_push(A, dmat_cast(opd));
	}
	return stfun_finalize(A);
}
/**
   Generate the structure function of the phase of kolmogorov spectrum.
*/
dmat* stfun_kolmogorov(loc_t* loc, real r0){
	long nloc=loc->nloc;
	real* locx=loc->locx;
	real* locy=loc->locy;
	dmat* st=dnew(nloc, nloc);
	dmat* B=st;
	real coeff=6.88*pow(r0, -5./3.)*pow(0.5e-6/(2.*M_PI), 2);
	for(int i=0; i<nloc; i++){
		for(int j=i; j<nloc; j++){
			real rdiff2=pow(locx[i]-locx[j], 2)+pow(locy[i]-locy[j], 2);
			P(B, i, j)=P(B, j, i)=coeff*pow(rdiff2, 5./6.);
		}
	}
	return st;
}
