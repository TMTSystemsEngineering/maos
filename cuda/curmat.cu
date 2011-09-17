extern "C"
{
#include <cuda.h>
#include "gpu.h"
}
#include "curmat.h"
#include "utils.h"
#include "kernel.h"
/**
   Createa curmat object.
*/
curmat *curnew(int nx, int ny){
    curmat *out;
    out=(curmat*)calloc(1, sizeof(curmat));
    out->ref=0;
    DO(cudaMalloc(&(out->p), nx*ny*sizeof(float)));
    DO(cudaMemset(out->p, 0, nx*ny*sizeof(float)));
    out->nx=nx;
    out->ny=ny;
    return out;
}

/**
   Createa curmat object.
*/
curmat *curnew(int nx, int ny, cudaStream_t stream){
    curmat *out;
    out=(curmat*)calloc(1, sizeof(curmat));
    out->ref=0;
    DO(cudaMalloc(&(out->p), nx*ny*sizeof(float)));
    DO(cudaMemsetAsync(out->p, 0, nx*ny*sizeof(float), stream));
    out->nx=nx;
    out->ny=ny;
    return out;
}
void curfree(curmat *A){
    if(A){
	if(A->p){
	    cudaFree(A->p);
	}
	free(A);
    }
}
void curzero(curmat *A, cudaStream_t stream){
    if(A && A->p){
	DO(cudaMemsetAsync(A->p, 0, A->nx*A->ny*sizeof(float), stream));
    }
}
void curset(curmat *A, float alpha, cudaStream_t stream){
    if(A && A->p){
	set_do<<<DIM(A->nx*A->ny,256),0,stream>>>(A->p, alpha, A->nx*A->ny);
    }
}
/**< Show the content of an array*/
void curshow(curmat *A, cudaStream_t stream){
    info("curshow: %dx%d\n", A->nx, A->ny);
    show_do<<<1,1,0,stream>>>(A->p, A->nx, A->ny);
}
void curcp(curmat **out, const curmat *in, cudaStream_t stream){
    if(!in){
	curzero(*out, stream);
    }else{
	if(!*out){
	    *out=curnew(in->nx, in->ny);
	}else{
	    assert((*out)->nx * (*out)->ny==in->nx * in->ny);
	}
	cudaMemcpyAsync((*out)->p, in->p, in->nx*in->ny*sizeof(float), cudaMemcpyDefault, stream);
	
    }
}
void curwritedata(const curmat *A, file_t *fp){
    if(A && A->nx>0 && A->ny>0){
	cudaDeviceSynchronize();
	float *tmp=(float*)malloc(A->nx*A->ny*sizeof(float));
	cudaMemcpy(tmp, A->p, A->nx*A->ny*sizeof(float), cudaMemcpyDefault);
	cudaDeviceSynchronize();
	do_write(fp, 0, sizeof(float), M_FLT, tmp, A->nx, A->ny);
	free(tmp);
    }else{
	do_write(fp, 0, sizeof(float), M_FLT, NULL, 0, 0);
    }
}
void curwrite(const curmat *A, const char *format, ...){
    format2fn;
    file_t *fp=zfopen(fn, "wb");
    curwritedata(A, fp);
    zfclose(fp);
}

/**
   add a vector to another, scaled by alpha and beta. all in device memory.
   a=a*alpha+b*beta;
*/
__global__ void add_do(float *restrict a, float alpha, 
		       const float *restrict b, float beta, int n){
    const int step=blockDim.x * gridDim.x;
    for(int i=blockIdx.x * blockDim.x + threadIdx.x; i<n; i+=step){
	a[i]=a[i]*alpha+b[i]*beta;
    }
}

__global__ static void scale_do(float *restrict in, int n, float alpha){
    const int step=blockDim.x * gridDim.x;
    for(int i=blockIdx.x * blockDim.x + threadIdx.x; i<n; i+=step){
	in[i]*=alpha;
    }
}

/**
   out=out*beta+in*alpha;
*/
void curadd(curmat **out, float alpha, curmat *in, float beta, cudaStream_t stream){
    if(!in) return;
    if(!*out || fabsf(alpha)<1e-7){
	curcp(out, in, stream);
	scale_do<<<DIM(in->nx*in->ny, 256),0,stream>>>
	    ((*out)->p, in->nx*in->ny, beta);
    }else{
	add_do<<<DIM(in->nx*in->ny, 256),0,stream>>>
	    ((*out)->p, alpha, in->p, beta, in->nx*in->ny);
    }
}
__global__ void addcabs2_do(float *restrict a, float alpha, 
			    const fcomplex *restrict b, float beta, int n){
    const int step=blockDim.x * gridDim.x;
    for(int i=blockIdx.x * blockDim.x + threadIdx.x; i<n; i+=step){
	a[i]=a[i]*alpha+CABS2(b[i])*beta;
    }
}
/**
   out=out*beta+abs2(in)*alpha;
*/
void curaddcabs2(curmat **out, float alpha, cucmat *in, float beta, cudaStream_t stream){
    if(!*out){
	*out=curnew(in->nx,in->ny);
    }else if(fabsf(alpha)<1e-7){
	curzero(*out, stream);
    }
    addcabs2_do<<<DIM(in->nx*in->ny, 256),0,stream>>>
	((*out)->p, alpha, in->p, beta, in->nx*in->ny);
}
void curscale(curmat *in, float alpha, cudaStream_t stream){
    int n=in->nx*in->ny;
    scale_do<<<DIM(n,256), 0, stream>>>(in->p, n, alpha); 
}

/**
   Computes C = alpha * C + beta * op(A) * B ;
*/
void curmm(curmat **C, float alpha, const curmat *A, const curmat *B, char trans[2], float beta, cublasHandle_t handle){
    int m,n,k,k2;
    cublasOperation_t transa, transb;
    if(trans[0]=='t'){
	m=A->ny;
	k=A->nx;
	transa=CUBLAS_OP_T;
    }else{
	m=A->nx;
	k=A->ny;
	transa=CUBLAS_OP_N;
    }
    if(trans[1]=='t'){
	n=B->nx;
	k2=B->ny;
	transb=CUBLAS_OP_T;
    }else{
	n=B->ny;
	k2=B->nx;
	transb=CUBLAS_OP_N;
    }
    if(!*C){
	*C=curnew(m,n);
    }else{
	assert((*C)->nx==m && (*C)->ny==n);
    }
    assert(k==k2);
    DO(cublasSgemm(handle, transa, transb, m,n,k,
		       &beta, A->p, A->nx, B->p, B->nx, &alpha, (*C)->p, (*C)->nx));
}
/**
   Computes C = alpha * C + beta * op(A) * B ;
*/
void curmv(curmat **C, float alpha, const curmat *A, const curmat *B, char trans, float beta, cublasHandle_t handle){
    if(!*C){
	*C=curnew(trans=='t'?A->ny:A->nx, 1);
    }else{
	assert((*C)->nx==(trans=='t'?A->ny:A->nx) && (*C)->ny==1);
    }
    cublasSgemv(handle, trans=='t'?CUBLAS_OP_T:CUBLAS_OP_N, A->nx, A->ny, &beta, A->p, A->nx, B->p, 1, &alpha, (*C)->p, 1);
}

curcell* curcellnew(int nx, int ny){
    curcell *out=(curcell*)calloc(1, sizeof(curcell));
    out->p=(curmat**)calloc(nx*ny, sizeof(void*));
    out->nx=nx;
    out->ny=ny;
    return out;
}

cuspcell* cuspcellnew(int nx, int ny){
    cuspcell *out=(cuspcell*)calloc(1, sizeof(cuspcell));
    out->p=(cusp**)calloc(nx*ny, sizeof(void*));
    out->nx=nx;
    out->ny=ny;
    return out;
}
curcell *curcellnew2(const curcell *in){
    curcell *out=(curcell*)calloc(1, sizeof(curcell));
    out->p=(curmat**)calloc(in->nx*in->ny, sizeof(void*));
    out->nx=in->nx;
    out->ny=in->ny;
    for(int i=0; i<in->nx*in->ny; i++){
	out->p[i]=curnew(in->p[i]->nx, in->p[i]->ny);
    }
    return out;
}

void curcellfree(curcell *A){
    if(!A) return;
    if(A->p){
	for(int i=0; i<A->nx*A->ny; i++){
	    curfree(A->p[i]);
	}
	free(A->p);
    }
    free(A);
}

void curcellwrite(const curcell *A, const char *format, ...){
    format2fn;
    file_t *fp=zfopen(fn, "wb");
    write_magic(MCC_ANY, fp);
    if(A){
	uint64_t nx=A->nx;
	uint64_t ny=A->ny;
	zfwritelarr(fp, 2, &nx, &ny);
	for(int i=0; i<A->nx*A->ny; i++){
	    curwritedata(A->p[i], fp);
	}
    }else{
	uint64_t zero=0;
	zfwritelarr(fp, 2, &zero, &zero);
    }
    zfclose(fp);	
}
void curcellzero(curcell *A, cudaStream_t stream){
    if(!A) return;
    for(int i=0; i<A->nx*A->ny; i++){
	curzero(A->p[i], stream);
    }
}
void curcellcp(curcell **A, const curcell *B, cudaStream_t stream){
    if(!B)
	curcellzero(*A, stream);
    else{
	if(!*A){
	    *A=curcellnew2(B);
	}else{
	    assert((*A)->nx * (*A)->ny == B->nx * B->ny);
	}
	for(int i=0; i<B->nx*B->ny; i++){
	    curcp(&(*A)->p[i], B->p[i], stream);
	}
    }
}
/*
  A=A*beta+B*alpha;
*/
void curcelladd(curcell **A, float beta, const curcell *B, float alpha, cudaStream_t stream){
    if(!B) return;
    if(!*A){
	*A=curcellnew2(B);
    }else{
	assert((*A)->nx==B->nx && (*A)->ny==B->ny);
    }
    for(int i=0; i<B->nx*B->ny; i++){
	curadd(&((*A)->p[i]), beta, B->p[i], alpha,stream);
    }
}


/**
   add a scalar alpha, scaled by beta to a vector. all in device memory.
*/
__global__ void adds_do(float *vec, float *palpha, float beta, int n){
    __shared__ float alpha;
    if(threadIdx.x==0) alpha=beta**palpha;
    const int step=blockDim.x * gridDim.x;
    for(int i=blockIdx.x * blockDim.x + threadIdx.x; i<n; i+=step){
	vec[i]+=alpha;
    }
}
/**
   add a vector to another, scaled by alpha and beta. all in device memory.
   a=a+b*alpha*beta;
*/
__global__ void add2_do(float *restrict a, const float *restrict b, const float *restrict b_sc1, float b_sc2, int n){
    float alpha=*b_sc1*b_sc2;
    const int step=blockDim.x * gridDim.x;
    for(int i=blockIdx.x * blockDim.x + threadIdx.x; i<n; i+=step){
	a[i]+=b[i]*alpha;
    }
}


/**
   out=out+in*alpha; beta, alpha lives in device memory.
*/
void curadd2(curmat **out, const curmat *in, float *alpha, cudaStream_t stream){
    if(!*out){
	*out=curnew(in->nx, in->ny);
    }
    add2_do<<<DIM(in->nx*in->ny, 256),0,stream>>>
	((*out)->p, in->p, alpha, 1.f, in->nx*in->ny);
}


/**
   A=A*beta+B*alpha; beta, alpha lives in device memory.
*/
void curcelladd2(curcell **A, const curcell *B, float* alpha, cudaStream_t stream){
    if(!B) return;
    if(!*A){
	*A=curcellnew2(B);
    }else{
	assert((*A)->nx==B->nx && (*A)->ny==B->ny);
    }
    for(int i=0; i<B->nx*B->ny; i++){
	curadd2(&((*A)->p[i]), B->p[i], alpha, stream);
    }
}

__global__ static void add3_do(float *restrict a, const float *restrict a_sc, const float a_sc2, const float *restrict b, int n){
    float alpha=*a_sc*a_sc2;
    const int step=blockDim.x * gridDim.x;
    for(int i=blockIdx.x * blockDim.x + threadIdx.x; i<n; i+=step){
	a[i]=a[i]*alpha+b[i];
    }
}
/**
   out=out*beta+in; beta, alpha lives in device memory.
*/
void curadd3(curmat **out, float *beta, const curmat *in, cudaStream_t stream){
    if(!*out){
	*out=curnew(in->nx, in->ny);
    }
    add3_do<<<DIM(in->nx*in->ny, 256),0,stream>>>
	((*out)->p, beta, 1.f, in->p, in->nx*in->ny);
}

/**
   A=A*beta+B*alpha; beta, alpha lives in device memory.
*/
void curcelladd3(curcell **A, float* beta, const curcell *B, cudaStream_t stream){
    if(!B) return;
    if(!*A){
	*A=curcellnew2(B);
    }else{
	assert((*A)->nx==B->nx && (*A)->ny==B->ny);
    }
    for(int i=0; i<B->nx*B->ny; i++){
	curadd3(&((*A)->p[i]), beta, B->p[i],  stream);
    }
}
__global__ void inn_do(float *restrict res, const float *a, const float *b, const int n){
    extern __shared__ float sb[];
    sb[threadIdx.x]=0;
    int step=blockDim.x * gridDim.x ;
    for(int i=blockIdx.x * blockDim.x + threadIdx.x; i<n; i+=step){
	sb[threadIdx.x]+=a[i]*b[i];
    }
    for(step=(blockDim.x>>1);step>0;step>>=1){
	__syncthreads();
	if(threadIdx.x<step){
	    sb[threadIdx.x]+=sb[threadIdx.x+step];
	}
    }
    if(threadIdx.x==0){
	atomicAdd(res, sb[0]);
    }
}
__global__ void sum_do(float *restrict res, const float *a, const int n){
    extern __shared__ float sb[];
    sb[threadIdx.x]=0;
    int step=blockDim.x * gridDim.x ;
    for(int i=blockIdx.x * blockDim.x + threadIdx.x; i<n; i+=step){
	sb[threadIdx.x]+=a[i];
    }
    for(step=(blockDim.x>>1);step>0;step>>=1){
	__syncthreads();
	if(threadIdx.x<step){
	    sb[threadIdx.x]+=sb[threadIdx.x+step];
	}
    }
    if(threadIdx.x==0){
	atomicAdd(res, sb[0]);
    }
}

inline static void inn_wrap(float *res, const float *restrict a, const float *restrict b, 
			    const int n, cudaStream_t stream){
    inn_do<<<DIM(n, DIM_REDUCE), DIM_REDUCE*sizeof(float), stream>>>(res,a,b,n);
}
inline static void sum_wrap(float *res, const float *restrict a, const int n, cudaStream_t stream){
    sum_do<<<DIM(n, DIM_REDUCE), DIM_REDUCE*sizeof(float), stream>>>(res,a,n);
}
float curinn(const curmat *a, const curmat *b, cudaStream_t stream){
    float *res;
    cudaMalloc(&res, sizeof(float));
    curinn2(res,a,b,stream);
    float out;
    cudaMemcpyAsync(&out, res, sizeof(float), cudaMemcpyDefault, stream);
    CUDA_SYNC_STREAM;
    return out;
}
float curcellinn(const curcell *A, const curcell *B, cudaStream_t stream){
    float out;
    static float *res=NULL;
    if(!res) cudaMalloc(&res, sizeof(float));
    curcellinn2(res, A, B, stream);
    cudaMemcpyAsync(&out, res, sizeof(float), cudaMemcpyDefault, stream);
    cudaStreamSynchronize(stream);
    return out;
}

void curinn2(float *restrict res, const curmat *a, const curmat *b, cudaStream_t stream){
    cudaMemsetAsync(res, 0,sizeof(float), stream);
    inn_wrap(res, a->p, b->p, a->nx*a->ny, stream);
}
/**
   res points to a scalar in device memory.
*/
void curcellinn2(float *restrict res, const curcell *A, const curcell *B, cudaStream_t stream){
    cudaMemsetAsync(res, 0,sizeof(float), stream);
    for(int i=0; i<A->nx*A->ny; i++){
	const curmat *a=A->p[i];
	const curmat *b=B->p[i];
	const int n=a->nx*a->ny;
	inn_wrap(res, a->p, b->p, n, stream);
    }
}

/**
   Sum all the elements in an array.
 */
void cursum2(float *restrict res, const curmat *a, cudaStream_t stream){
    cudaMemsetAsync(res, 0,sizeof(float), stream);
    sum_wrap(res, a->p, a->nx*a->ny, stream);
}