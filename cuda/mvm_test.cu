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
#include "utils.h"
#include "curmat.h"
#include "cucmat.h"

/**
\file mvm_test.cu

Test performance of different cuda implementations for matrix vector multiplication.
Make sure the results is correct first.
Test using transpose.

*/
typedef float ATYPE;
typedef float GTYPE;

#define tix threadIdx.x
#define tiy threadIdx.y
#define bix blockIdx.x
#define biy blockIdx.y
#define tnx blockDim.x
#define tny blockDim.y
#define bnx gridDim.x
#define bny gridDim.y

#define BLOCKMV_TNY 1
#define BLOCKMV_TNX 256
/*
  y=A*x;
  Call with
  blockmv<<<ngrid, dim(tnx, tny), (tny+tnx*tny)*sizeof(float), stream>>>()
  with both tnx and tny must be power of two and be less than 32. tny must be 32.
*/
__global__ static void
blockmv(float *restrict y, const float *restrict A, const float *restrict x, const int nrow, const int ncol){
    __shared__ float shared[BLOCKMV_TNX][BLOCKMV_TNY];
    //float *sx=shared;//stores x in shared memory
    register float *sy=&shared[tix][tiy];//stores non-reduced y in shared memory, transposed for easy reduction.
    register const int irow=(tnx*bix+tix);
    *sy=0;
#if 1
    if(irow<nrow){
	for(int icol=tiy; icol<ncol; icol+=tny){//a few pass
	    *sy+=A[irow+icol*nrow]*x[icol];
	}
    }
    //printf("0 (%02d %02d) =%g\n", tix, tiy, *sy);
#else
    for(int ip=0; ip<ncol; ip+=tny){//a few pass
	const int icol=ip+tiy;
	if(tix==0){
	    if(icol<ncol){
		sx[tiy]=x[icol];
	    }else{
		sx[tiy]=0;
	    }
	}
	__syncthreads();
	if(irow<nrow){
	    *sy+=A[irow+icol*nrow]*sx[tiy];
	}
    }
#endif
    /*
#if BLOCKMV_TNY==32
    if(irow<nrow){
#pragma unroll
	for(int step=(BLOCKMV_TNY>>1); step>0; step>>=1){
	    __syncthreads();
	    if(tiy<step){
		sy[0]+=sy[step];
	    }
	}
	if(!tiy){
	    atomicAdd(&y[irow], sy[0]);
	}
    }
#else
    __syncthreads();
    if(tiy==0 && irow<nrow){
#pragma unroll
	for(int i=1; i<BLOCKMV_TNY; i++){
	    sy[0]+=sy[i];
	}
        atomicAdd(&y[irow], sy[0]);
    }
    #endif*/
}
/**
  Another version to increase the occpancy. 
  multimv<<<ngrid, dim(tnx, tny), (tnx*tny)*sizeof(float), stream>>>()
  tny can be any number. tnx is ~256/tny.
*/
/*__global__ static void
multimv(float *restrict y, const float *restrict A, const float *restrict x, const int nrow, const int ncol){

}*/
__global__ static void
test_read(float *A, int nx, int ny){
    //extern __shared__ float sh[];
    register const int irow=tnx*bix+tix;
    float __shared__ sum;
    sum=0;
    for(int i=0; i<ny; i++){
	sum+=A[irow+i*nx];
    }
}
__global__ static void
test_read_multi(float *A, int nx, int ny){
    //extern __shared__ float sh[];
    register int irow=tnx*bix+tix;
    const int nset=(tnx*bnx+nx-1)/nx;
    const int iset=irow/nx;
    irow=irow-iset*nx;
    float __shared__ sum;
    sum=0;
    for(int i=iset*ny/nset; i<(iset+1)*ny/nset; i++){
	sum+=A[irow+i*nx];
    }
}

void mvm_test(int igpu){
    cudaSetDevice(igpu);
    int M,N;
    M=6981;
    N=2895*2;
    //N=2048*2;
    //N=4000;
    int nstream=5;
    //M=32*32*32;
    //N=600;
    //M=32;
    //N=32*32;
    int iN=1200;
    iN=N;
    smat *mvm=snew(M,N);
    smat *x=snew(N,1);
    rand_t stat;
    seed_rand(&stat, 1);
    srandn(mvm, 1, &stat);
    srandn(x, 1, &stat);
    //swrite(mvm, "mvm");
    //swrite(x, "x");
    smat *mvmt=strans(mvm);
    curmat *cumvmt=NULL;
    curmat *cumvm=NULL;
    curmat *cux=NULL, *cuy=NULL;
    cp2gpu(&cumvm, mvm);
    cp2gpu(&cumvmt, mvm);
    cp2gpu(&cux, x);
    cuy=curnew(M, 1);
    stream_t stream[nstream];
    int nevent=nstream*2;
    event_t event[nevent];
    float one=1.;
    //curwrite(cuy, "y0");
    float tm;
    cudaProfilerStart();
    
    {
	event[0].record(stream[0]);
	for(int i=0; i<N; i+=iN){
	    int nleft=N-i;
	    if(nleft>iN) nleft=iN;
	    DO(cublasSgemv(stream[0], CUBLAS_OP_N, M,nleft, &one, cumvm->p+i*M, M, cux->p+i, 1, &one, cuy->p, 1));
	}
	event[1].record(stream[0]);
	stream[0].sync();
	DO(cudaEventElapsedTime(&tm, event[0], event[1]));
	info("cublasSgemv takes %.6f ms\n", tm);
	curwrite(cuy, "y_cugemv");
    }
    {
	for(int nover=2; nover<256; nover+=2)
	    {
		//custom method. 10 is optimal
		int naeach=128;
		const int nblock=(M*nover+naeach-1)/naeach;
		cuy->zero();
		int is;
		for(is=0; is<nstream; is++){
		    stream[is].sync();
		}
		TIC;tic;
		//event[0].record(stream[0]);
		is=0;
		for(int i=0; i<N; i+=iN){
		    int nleft=N-i;
		    if(nleft>iN) nleft=iN;
		    multimv_do<<<nblock, naeach, sizeof(float)*naeach, stream[is]>>>
			(cumvm->p+i*M, cuy->p, cux->p+i, M, nleft);
		    is=(is+1)%nstream;
		}
		//event[1].record(stream[0]);
		for(is=0; is<nstream; is++){
		    stream[is].sync();
		}
		info("nover=%d,  %.6f ms\n", nover, toc3*1000);
	    }
	curwrite(cuy, "y_multimv");
    }
    {
	//custom method
	int naeach, mp_count;
	naeach=128;
	mp_count=(M+naeach-1)/naeach;
	cuy->zero(stream[0]);stream[0].sync();
    	event[0].record(stream[0]);
	for(int i=0; i<N; i+=iN){
	    int nleft=N-i;
	    if(nleft>iN) nleft=iN;
	    mvm_do<<<mp_count, naeach, sizeof(float)*naeach, stream[0]>>>
		(cumvm->p+i*M, cuy->p, cux->p+i, M, nleft);
	}
	event[1].record(stream[0]);
	stream[0].sync();
	DO(cudaEventElapsedTime(&tm, event[0], event[1]));
	info("mvm_g_mul takes %.6f ms\n", tm);
	curwrite(cuy, "y_gmul");
	}
    /*  {
	
	const int nblock=(M+BLOCKMV_TNX-1)/BLOCKMV_TNX;
	//const int nblock=1;
	//new method with blocking
	cuy->zero(stream[0]);stream[0].sync();
	cudaProfilerStart();
	event[0].record(stream[0]);
	for(int i=0; i<N; i+=iN){
	    int nleft=N-i;
	    if(nleft>iN) nleft=iN;
	    blockmv<<<nblock, dim3(BLOCKMV_TNX, BLOCKMV_TNY), 0, stream[0]>>>
		(cuy->p, cumvm->p+i*M, cux->p+i, M, nleft);
	}
	event[1].record(stream[0]);
	stream[0].sync();
	cudaProfilerStop();
	DO(cudaEventElapsedTime(&tm, event[0], event[1]));
	info("blockmv takes %.6f ms\n", tm);
	curwrite(cuy, "y_blockmv");
	}*/
    /*{
	cudaProfilerStart();
	int nthread=192;
	int nblock=M/nthread;
	//TIC;tic;
	for(int is=0; is<nstream; is++){
	    event[2*is].record(stream[is]);
	    test_read<<<nblock, nthread, 0, stream[is]>>>
		(cumvm->p+M*N/nstream*is, M, N/nstream);
	    event[2*is+1].record(stream[is]);
	}
	for(int is=0; is<nstream; is++){
	    stream[is].sync();
	}
	//toc("sync");
	cudaProfilerStop();
	for(int is=0; is<nstream; is++){
	    float tm1, tm2;
	    DO(cudaEventElapsedTime(&tm1, event[0], event[2*is]));
	    DO(cudaEventElapsedTime(&tm2, event[0], event[2*is+1]));
	    info2("%.6f %.6f %.6f\n", tm1, tm2, tm2-tm1);
	}
	DO(cudaEventElapsedTime(&tm, event[0], event[nevent-1]));
	info("test_read takes %.6f ms, BW=%.1f GB/s\n", tm, M*N*4/tm*1e-6);

	}*/

    /*{
	
	cudaProfilerStart();
	const int ncase=100;
	int nthread=128;//192 or 256 are good values
	for(N=4000; N>10; N=N/2){
	    int i0;
	    float t0=INFINITY;
	    for(nstream=2; nstream<=512; nstream+=2){
		int nblock=M/nthread*nstream;
		float tm0=0;
		for(int icase=0; icase<ncase; icase++){
		    //A single stream, but different kernels does different columns.
		    //nstream=256;
		    event[0].record(stream[0]);
		    test_read_multi<<<nblock, nthread, 0, stream[0]>>>
			(cumvm->p, M, N);
		    event[1].record(stream[0]);
		    stream[0].sync();
		    DO(cudaEventElapsedTime(&tm, event[0], event[1]));
		    tm0+=tm;
		}
		if(tm0<t0){
		    t0=tm0;
		    i0=nstream;
		}
	
	    }
info2("N=%d nstream=%d, BW=%g\n", N, i0, M*N*4/t0*1e-6*ncase);
	}
	cudaProfilerStop();
    }*/
}