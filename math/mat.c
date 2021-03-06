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

#include <errno.h>
#include "random.h"
#include "mathdef.h"
#include "defs.h"/*Defines T, X, etc */

/**
   The only function that actually creats the matrix object. It ensures that all
   fields are properly initialized. If p is NULL, memory is allocated. If ref is
   true, p is treated as external resource and is not reference counted.
*/
X(mat)* X(new_do)(long nx, long ny, T* p, mem_t* mem){
	if(!p){//no pointer is supplied
		if(nx>0&&ny>0){
			if(!(p=mycalloc((nx*ny), T))){
				error("malloc for %ldx%ld of size %ld failed.\n", nx, ny, sizeof(T));
			}
			if(mem){
				print_backtrace();
				error("mem=%p must be NULL when p is NULL.\n", mem);
			}
			mem=mem_new(p);
		}
	}
	if(nx<=0 || ny<=0){
		nx=0;
		ny=0;
	}
	X(mat)* out=mycalloc(1, X(mat));
	if(!out) {//failed
		free(p);
		mem_unref(&mem);
	}else{
		out->id=M_T;
		out->nx=nx;
		out->ny=ny;
		out->p=p;
		if(mem) out->mem=mem_ref(mem);
	}
	return out;
}

/**
   creat a reference an existing matrix. Use the reference carefully.
*/
X(mat)* X(ref)(const X(mat)* in){
	if(!in) return NULL;
	X(mat)* out=X(new_do)(in->nx, in->ny, P(in), in->mem);
	if(out&&in->header) out->header=strdup(in->header);
	return out;
}

/**
   Create a new T matrix object. initialized all to zero.
*/
 X(mat)* X(new)(long nx, long ny){
	return X(new_do)(nx, ny, NULL, 0);
}
/**
 * Calls X(new) with a filename to be saved to
 * */
X(mat)* X(new_file)(long nx, long ny, const char* header, const char* format, ...){
	if(!nx||!ny) return NULL;
	format2fn;
	if(fn&&fn[0]=='-') fn=NULL;//leading - disables filename.
	X(mat)* out=X(new)(nx, ny);
	if(out&&header) out->header=strdup(header);
	if(disable_save&&!IS_SHM(fn))fn=NULL;
	if(out&&fn) {
		out->fp=zfopen(fn, "w");
		writedata_by_id(out->fp, CELL(out), 0, -1);
		//out->async=async_init(out->fp, sizeof(T), M_T, out->header, P(out), out->nx, out->ny);
	}
	return out;
}
/**
   check the size of matrix if exist. Otherwise create it. content is not zeroed.
*/
void X(init)(X(mat)** A, long nx, long ny){
	if(!*A){
		*A=X(new)(nx, ny);
	} else if((*A)->nx==0||(*A)->ny==0){
		X(resize)(*A, nx, ny);
	} else if((*A)->nx!=nx||(*A)->ny!=ny){
		error("Mismatch: A is %ldx%ld, want %ldx%ld\n",
			(*A)->nx, (*A)->ny, nx, ny);
	}
}
/**
   cast a cell object to matrix
 */
X(mat)* X(mat_cast)(const cell* A){
	return (A && A->nx && A->ny && ismat(A))?(X(mat)*)A:NULL;
}
/**
   free a matrix object.
*/
void X(free_do)(X(mat)* A){
	if(check_mat(A)){
		//only need to initiate write when fp is set.
		//if(A->fp) X(writedata)(A->fp, A, A->ny);//don't do this. 
		if(A->async){//make sure entire data is written.
			async_write(A->async, A->nx*A->ny*sizeof(T), 1);
			async_free(A->async); A->async=NULL;
		}
		if(A->fp) {zfclose(A->fp); A->fp=NULL;}
		mem_unref(&A->mem);//takes care of freeing memory.
		
#ifndef COMP_LONG
		if(A->fft) X(fft_free_plan)(A->fft);
#endif
		if(A->header) {free(A->header); A->header=NULL;}
	}
	free(A);
}

/**
   create an new reference another with different shape.
*/
X(mat)* X(ref_reshape)(const X(mat)* in, long nx, long ny){
	if(!check_mat(in)) return NULL;
	X(mat)* out=X(ref)(in);
	reshape(out, nx, ny);
	out->nx=nx;
	out->ny=ny;
	return out;
}

/**
   creat a new matrix referencing columns in existing
   matrix. reference counted. not used
*/
X(mat)* X(refcols)(const X(mat)* in, long icol, long ncol){
	if(!check_mat(in)) return NULL;
	return X(new_do)(in->nx, ncol, PCOL(in, icol), in->mem);
}

/**
   Create a new sub matrix of nx*ny starting from(sx,sy) and copy the data.
*/
X(mat)* X(sub)(const X(mat)* in, long sx, long nx, long sy, long ny){
	if(!check_mat(in)) return NULL;
	if(nx<=0){
		nx=in->nx-sx;
	}
	if(ny<=0){
		ny=in->ny-sy;
	}
	if(sx+nx>in->nx||sy+ny>in->ny){
		error("Invalid parameter range: (%ld-%ld)x(%ld-%ld) is outside of %ldx%ld\n",
			sx, sx+nx, sy, sy+ny, in->nx, in->ny);
		return NULL;
	} else{
		X(mat)* out=X(new)(nx, ny);
		for(int iy=0; iy<ny; iy++){
			memcpy(PCOL(out, iy), &P(in, sx, iy+sy), sizeof(T)*nx);
		}
		return out;
	}
}
/**
   Resize a matrix by adding or removing columns or rows. Data is kept whenever
   possible.
*/
void X(resize)(X(mat)* A, long nx, long ny){
	if(!A) {
		if(!nx || !ny){
			return;
		}else{
			error("Trying to resize NULL array to %ldx%ld\n", nx, ny);
		}
	}
	else if(!ismat(A)){
		if(iscell(A)&&!P(A)&&(A->nx==0||A->ny==0)){
			A->id=M_T;//convert empty cell to mat.
		} else{
			warning("Incorrect type: id=%d\n", A->id);
			return;
		}
	}
	if(!nx) nx=A->nx;
	if(!ny) ny=A->ny;
	if(mem_nref(A->mem)>1){
		error("Trying to resize referenced matrix\n");
		return;
	}
	if(A->nx!=nx||A->ny!=ny){
		if(A->nx==nx||A->ny==1){
			P(A)=myrealloc(P(A), nx*ny, T);
			if(nx*ny>PN(A)){
				memset(P(A)+PN(A), 0, (nx*ny-PN(A))*sizeof(T));
			}
		} else{/*copy memory to preserve data*/
			T* p=mycalloc(nx*ny, T);
			if(!p){
				error("malloc for %ldx%ld of size %ld failed.\n", nx, ny, sizeof(T));
			}
			long minx=A->nx<nx?A->nx:nx;
			long miny=A->ny<ny?A->ny:ny;
			for(long iy=0; iy<miny; iy++){
				memcpy(p+iy*nx, PCOL(A, iy), sizeof(T)*minx);
			}
			free(P(A));
			P(A)=p;
		}
		if(A->mem){
			mem_replace(A->mem, P(A));
		} else{
			A->mem=mem_ref(mem_new(P(A)));
		}
		A->nx=nx;
		A->ny=ny;
	}
}

/**
   Concatenate two matrixes into 1 along dimension "dim"
*/
X(mat)* X(cat)(const X(mat)* in1, const X(mat)* in2, int dim){
	if(!check_mat(in2)){
		if(check_mat(in1)){
			return X(dup)(in1);
		} else{
			return NULL;
		}
	} else if(!check_mat(in1)){
		return X(dup)(in2);
	}
	X(mat)* out=NULL;

	if(dim==1){
		/*along x. */
		if(in1->ny!=in2->ny){
			error("Mismatch. in1 is (%ld, %ld), in2 is (%ld, %ld)\n",
				in1->nx, in1->ny, in2->nx, in2->ny);
		} else{
			out=X(new)(in1->nx+in2->nx, in1->ny);
			for(long iy=0; iy<in1->ny; iy++){
				memcpy(PCOL(out, iy), PCOL(in1, iy), in1->nx*sizeof(T));
				memcpy(PCOL(out, iy)+in1->nx, PCOL(in2, iy), in2->nx*sizeof(T));
			}
		}
	} else if(dim==2){
   		/*along y. */
		if(in1->nx!=in2->nx){
			error("Mismatch. in1 is (%ld, %ld), in2 is (%ld, %ld)\n",
				in1->nx, in1->ny, in2->nx, in2->ny);
		} else{
			out=X(new)(in1->nx, in1->ny+in2->ny);
			memcpy(P(out), P(in1), in1->nx*in1->ny*sizeof(T));
			memcpy(P(out)+PN(in1), P(in2), in2->nx*in2->ny*sizeof(T));
		}
	} else{
		error("Invalid dim, excepts 1 or 2.\n");
	}
	return out;
}
/**
   Set elements to zero.
*/
void X(zero)(X(mat)* A){
	if(A){
		memset(P(A), 0, sizeof(T)*A->nx*A->ny);
	}
}
/**
   Set column elements to zero.
*/
void X(zerocol)(X(mat)* A, int icol){
	if(check_mat(A)){
		if(icol>=0&&icol<A->ny){
			memset(PCOL(A, icol), 0, sizeof(T)*A->nx);
		} else{
			warning("Invalid range.\n");
		}
	}
}
/**
   Compute the hashlittle
 */
uint32_t X(hash)(const X(mat)* A, uint32_t key){
	if(check_mat(A)){
		key=hashlittle(P(A), A->nx*A->ny*sizeof(T), key);
	}
	return key;
}
/**
   copy the values from one matrix to another.
*/
void X(cp)(X(mat)** out0, const X(mat)* in){
	if(check_mat(in)){
		X(init)(out0, in->nx, in->ny);
		X(mat)* out=*out0;
		if(in->header){
			free(out->header);
			out->header=strdup(in->header);
		}
		if(P(out)!=P(in)){
			memcpy(P(out), P(in), in->nx*in->ny*sizeof(T));
		}
	} else{
		X(zero)(*out0);
	}
}

/**
   duplicate a matrix
*/
X(mat)* X(dup)(const X(mat)* in){
	if(!check_mat(in)) return NULL;
	X(mat)* out=NULL;
	X(cp)(&out, in);
	return out;
}

/**
   transpose a matrix
*/
X(mat)* X(trans)(const X(mat)* A){
	if(!check_mat(A)) return NULL;
	X(mat)* B=X(new)(A->ny, A->nx);
	if(A->nx==1||A->ny==1){
		memcpy(P(B), P(A), A->nx*A->ny*sizeof(T));
	} else{
		for(int iy=0; iy<A->ny; iy++){
			for(int ix=0; ix<A->nx; ix++){
				P(B, iy, ix)=P(A, ix, iy);
			}
		}
	}
	return B;
}
/**
   set values of each element in an matrix to val.
*/
void X(set)(X(mat)* A, const T val){
	if(!check_mat(A)) return;
	for(long i=0; i<A->nx*A->ny; i++){
		P(A, i)=val;
	}
}

/**
   display a matrix.
*/
void X(show)(const X(mat)* A, const char* format, ...){
	format2fn;
	info("Displaying content of %s:\n", fn);
	if(!check_mat(A)) return;
	int colmax=10;
	int iset, i, j;
	int nset=(A->ny+colmax-1)/colmax;
	for(iset=0; iset<nset; iset++){
		int ncol=(iset+1)*colmax;
		if(ncol>A->ny) ncol=A->ny;
		info("Cols %d to %d\n", iset, ncol-1);
		for(j=0; j<A->nx; j++){
			for(i=iset*colmax; i<ncol; i++){
				PRINT(P(A, j, i));
			}
			info("\n");
		}
	}
}
/**
   Permute the vector so that
   out(:)=in(p);
*/
void X(vecperm)(X(mat)* out, const X(mat)* in, const long* perm){
	if(!check_mat(in)||!check_mat(out)||!check_match(in, out)) return;
	for(long i=0; i<in->nx*in->ny; i++){
		if(perm[i]>0){
			P(out, i)=P(in, perm[i]);
		} else{
			P(out, i)=conj(P(in, -perm[i]));
		}
	}
}
/**
   Reverse permute the vector so that
   out(p)=in(:);
*/
void X(vecpermi)(X(mat)* out, const X(mat)* in, const long* perm){
	if(!check_mat(in)||!check_mat(out)||!check_match(in, out)) return;
	assert(in->nx*in->ny==out->nx*out->ny);
	for(long i=0; i<in->nx*in->ny; i++){
		if(perm[i]>0){
			P(out, perm[i])=P(in, i);
		} else{
			P(out, -perm[i])=conj(P(in, i));
		}
	}
}
/**
 * Flip the matrix along the set axis. 0 to flip both, 1 along x, 2 along y.
 * */
void X(flip)(X(mat)* A, int axis){
	if(!A || axis<0 || axis>2) {
		dbg("flip: A is null or axis is invalid: %p %d\n", A, axis);
		return;
	}
	const long xoff=(axis==0||axis==1)?(A->nx-1):0;
	const long xscale=xoff?-1:0;
	const long yoff=(axis==0||axis==2)?(A->ny-1):0;
	const long yscale=yoff?-1:0;
	const long ny=A->ny;
	const long nx2=A->nx/2;
	for(long iy=0; iy<ny; iy++){
		for(long ix=0; ix<nx2; ix++){
			T* ptmp=&P(A, xoff+xscale*ix, yoff+yscale*iy);
			T tmp=*ptmp;
			*ptmp=P(A, ix, iy);
			P(A, ix, iy)=tmp;
		}
	}

}
/**
   create sum of all the elements in A.
*/
T X(sum)(const X(mat)* A){
	T v=0;
	if(check_mat(A)){
		v=X(vecsum)(P(A), A->nx*A->ny);
	}
	return v;
}
/**
   compute the trace (sum of diagonal elements)
*/
T X(trace)(const X(mat)* A){
	TD trace=0;
	if(check_mat(A)){
		long n=MIN(A->nx, A->ny);
		for(long i=0; i<n; i++){
			trace+=P(A, i, i);
		}
	}
	return (T)trace;
}

#ifndef COMP_COMPLEX
static int sort_ascend(const T* A, const T* B){
	if((*A)>(*B)) return 1;
	else return -1;
}
static int sort_descend(const T* A, const T* B){
	if((*A)>(*B)) return -1;
	else return 1;
}
/**
  Sort all columns of A, in ascending order if ascend is non zero, otherwise in descending order.
*/
void X(sort)(X(mat)* A, int ascend){
	if(!check_mat(A)) return;
	for(int i=0; i<A->ny; i++){
		qsort(PCOL(A, i), A->nx, sizeof(R),
			(int(*)(const void*, const void*))(ascend?sort_ascend:sort_descend));
	}
}
#endif


/**
   find the maximum value
*/
R X(max)(const X(mat)* A){
	if(!check_mat(A)) return 0;
	R max, min;
	X(maxmin)(P(A), A->nx*A->ny, &max, &min);
	return max;
}

/**
   find the minimum value
*/
R X(min)(const X(mat)* A){
	if(!check_mat(A)) return 0;
	R max, min;
	X(maxmin)(P(A), A->nx*A->ny, &max, &min);
	return min;
}
/**
   find the maximum of abs of all elements
*/
R X(maxabs)(const X(mat)* A){
	if(!check_mat(A)) return 0;
	return X(vecmaxabs)(P(A), A->nx*A->ny);
}
/**
   compute the sum of abs of all elements
*/
R X(sumabs)(const X(mat)* A){
	if(!check_mat(A)) return 0;
	R out=0;
	for(long i=0; i<A->nx*A->ny; i++){
		out+=fabs(P(A, i));
	}
	return out;
}
/**
   compute the sum of A.*A
*/
R X(sumsq)(const X(mat)* A){
	if(!check_mat(A)) return 0;
	R out=0;
	for(long i=0; i<A->nx*A->ny; i++){
		out+=ABS2(P(A, i));
	}
	return out;
}
/**
   compute the sum of (A-B)^2
*/
R X(sumdiffsq)(const X(mat)* A, const X(mat)* B){
	if(!check_mat(A)||!check_mat(B)||!check_match(A, B)) return -1;
	RD out=0;
	for(long i=0; i<A->nx*A->ny; i++){
		out+=ABS2(P(A, i)-P(B, i));
	}
	return (R)out;
}


/**
   shift frequency components by n/2
*/
void X(fftshift)(X(mat)* A){
	if(!check_mat(A)) return;
	long i;
	const long nx=A->nx;
	const long ny=A->ny;
	if((nx&1)==1){
		warning_once("nx=%ld is not multiple of 2\n", nx);
	}
	const long nx2=nx/2;
	const long ny2=ny/2;
	const long nx2d=nx2*sizeof(T);
	T* tmp=(T*)malloc(nx2d);
	T* data=P(A);
	if(ny==1){
		memcpy(tmp, data, nx2d);
		memcpy(data, data+nx2, nx2d);
		memcpy(data+nx2, tmp, nx2d);
	} else{
		assert((ny&1)==0);
		for(i=0; i<ny2; i++){
			memcpy(tmp, data+i*nx, nx2d);
			memcpy(data+i*nx, data+(i+ny2)*nx+nx2, nx2d);
			memcpy(data+(i+ny2)*nx+nx2, tmp, nx2d);
			memcpy(tmp, data+i*nx+nx2, nx2d);
			memcpy(data+i*nx+nx2, data+(i+ny2)*nx, nx2d);
			memcpy(data+(i+ny2)*nx, tmp, nx2d);
		}
	}

	free(tmp);
}

/**
   reorder B and embed/crop into center of A .

   \verbatim
   4 * * 3
   * * * *
   * * * *
   2 * * 1
   \endverbatim
   to
   \verbatim
   1 2
   3 4
   \endverbatim
*/
void X(cpcorner2center)(X(mat)* A, const X(mat)* B){
	if(!check_mat(A)||!check_mat(B)) return;
	const long Anx=A->nx;
	const long Any=A->ny;
	const long Bnx=B->nx;
	const long Bny=B->ny;
	if(Anx>Bnx||Any>Bny){
		X(zero)(A);
	}
	if(!((Anx&1)==0&&(Any&1)==0&&(Bnx&1)==0&&(Bny&1)==0)){
		warning("Arrays with odd index may not work as expected\n");
	}
	const int ny2=MIN(Any, Bny)>>1;
	const int nx2=MIN(Anx, Bnx)>>1;
	const int xoff=Anx/2-nx2;
	const int yoff=Any/2-ny2;
	const int nx2d=nx2*sizeof(T);
	for(int i=0; i<ny2; i++){
		memcpy(&P(A, xoff, 		yoff+i), 	 &P(B, Bnx-nx2,	Bny-ny2+i), nx2d);
		memcpy(&P(A, xoff+nx2, 	yoff+i),	 &P(B, 0, 	  	Bny-ny2+i), nx2d);
		memcpy(&P(A, xoff, 		yoff+i+ny2), &P(B, Bnx-nx2,	i), 		nx2d);
		memcpy(&P(A, xoff+nx2, 	yoff+i+ny2), &P(B, 0, 		i), 		nx2d);
	}
}

/**
   cyclic shift A by nx and ny to B.
   \verbatim
   4   3     1   2

   2   1 to  3   4
   \endverbatim
*/
void X(shift)(X(mat)** B0, const X(mat)* A, int sx, int sy){
	if(!check_mat(A)) return;
	X(init)(B0, A->nx, A->ny);
	X(mat)* B=*B0;

	const int nx=A->nx;
	const int ny=A->ny;
	sx=sx%nx; if(sx<0) sx+=nx;
	sy=sy%ny; if(sy<0) sy+=ny;
	if(sx!=0||sy!=0){
		int dy=ny-sy;
		int dx=nx-sx;
		for(int iy=0; iy<sy; iy++){
			memcpy(&P(B, 0, iy), &P(A, dx, iy+dy), sx*sizeof(T));/*3 */
			memcpy(&P(B, sx, iy), &P(A, 0, iy+dy), dx*sizeof(T));/*4 */
		}
		for(int iy=sy; iy<ny; iy++){
			memcpy(&P(B, 0, iy), &P(A, dx, iy-sy), sx*sizeof(T));/*1 */
			memcpy(&P(B, sx, iy), &P(A, 0, iy-sy), dx*sizeof(T));/*2 */
		}
	} else{
		X(cp)(B0, A);
	}
}
/**
   cast a cell object to actual cell after checking.
 */
X(cell)* X(cell_cast)(const cell* A){
	if(!iscell(A)) return NULL;
	for(int i=0; i<A->nx*A->ny; i++){
		if(P(A, i)&&!ismat(P(A, i))){
			warning("A[%d] is not mat, return NULL.\n", i);
			return NULL;
		}
	}
	return (X(cell)*)A;
}
/**
   create an new cell similar to A in shape.
   When a cell is empty, it is created with an emtry matrix and cannot be overriden.
*/
X(cell)* X(cellnew2)(const X(cell)* A){
	if(!A) return NULL;
	X(cell)* out=X(cellnew)(A->nx, A->ny);
	long tot=0;
	for(long i=0; i<A->nx*A->ny; i++){
		if(!isempty(P(A, i))){
			tot+=P(A, i)->nx*P(A, i)->ny;
		}
	}
	out->m=X(new)(tot, 1);
	tot=0;
	for(int i=0; i<A->nx*A->ny; i++){
		if(!isempty(P(A, i))){
			P(out, i)=X(new_do)(P(A, i)->nx, P(A, i)->ny, P(out->m)+tot, out->m->mem);
			tot+=P(A, i)->nx*P(A, i)->ny;
		} else{
			P(out, i)=X(new)(0, 0);//place holder to avoid been overriden.
		}
	}
	return out;
}

/**
   Create an new cell with matrix specified. Each block is stored continuously in memory.
*/
X(cell)* X(cellnew3)(long nx, long ny, long* nnx, long* nny){
	long tot=0;
	for(long i=0; i<nx*ny; i++){
		long mx=(long)nnx>0?nnx[i]:(-(long)nnx);
		long my=nny?((long)nny>0?nny[i]:(-(long)nny)):1;
		tot+=mx*my;
	}
	X(cell)* out=X(cellnew)(nx, ny);
	
	out->m=X(new)(tot, 1);
	tot=0;
	for(long i=0; i<nx*ny; i++){
		long mx=(long)nnx>0?nnx[i]:(-(long)nnx);
		long my=nny?((long)nny>0?nny[i]:(-(long)nny)):1;
		P(out, i)=X(new_do)(mx, my, P(out->m)+tot, out->m->mem);
		tot+=mx*my;
	}
	
	return out;
}
/**
   Create an new cell with matrix specified. Each block is stored continuously in memory.
*/
X(cell)* X(cellnew_same)(long nx, long ny, long mx, long my){
	return X(cellnew3)(nx, ny, (long*)-mx, (long*)-my);
}
/**
   Calls cellnew3 with a filename to be saved to.
*/
X(cell)* X(cellnew_file)(long nx, long ny, long* nnx, long* nny,
	const char* header, const char* format, ...){
	if(!nx||!ny) return NULL;
	format2fn;
	if(fn&&fn[0]=='-') fn=NULL;//leading - disables filename.
	X(cell)* out=X(cellnew3)(nx, ny, nnx, nny);
	if(out&&header) out->header=strdup(header);
	if(disable_save&&!IS_SHM(fn))fn=NULL;
	if(out && fn) {
		out->fp=zfopen(fn, "w");
		writedata_by_id(out->fp, CELL(out), 0, -1);
	}
	return out;
}
/**
   Calls cellnew_same with a filename to be saved to.
*/
X(cell)* X(cellnewsame_file)(long nx, long ny, long mx, long my,
	const char* header, const char* format, ...){
	if(!nx||!ny) return NULL;
	format2fn;
	if(fn&&fn[0]=='-') fn=NULL;//leading - disables filename.
	X(cell*) out=X(cellnew_same)(nx, ny, mx, my);
	if(out&&header) out->header=strdup(header);
	if(disable_save&&!IS_SHM(fn))fn=NULL;
	if(out && fn) {
		out->fp=zfopen(fn, "w");
		writedata_by_id(out->fp, CELL(out), 0, -1);
	}
	return out;
}
/**
   creat a cell reference an existing cell by referencing the
   elements.
*/
X(cell)* X(cellref)(const X(cell)* in){
	if(!in) return NULL;
	X(cell)* out=X(cellnew)(in->nx, in->ny);
	if(in->m){
		out->m=X(ref)(in->m);
	}
	for(int i=0; i<in->nx*in->ny; i++){
		P(out, i)=X(ref)(P(in, i));
	}
	if(in->header) out->header=strdup(in->header);
	return out;
}

/**
   setting all elements of a cell to alpha.
*/
void X(cellset)(X(cell)* dc, T val){
	if(dc){
		for(int ix=0; ix<dc->nx*dc->ny; ix++){
			if(P(dc, ix)) X(set)(P(dc, ix), val);
		}
	}
}

/**
   transpose a cell object
*/
X(cell)* X(celltrans)(const X(cell)* A){
	if(!A) return NULL;
	X(cell)* B=X(cellnew)(A->ny, A->nx);
	for(int ix=0; ix<A->nx; ix++){
		for(int iy=0; iy<A->ny; iy++){
			P(B, iy, ix)=X(trans)(P(A, ix, iy));
		}
	}
	return B;
}

/**
   reduce nx*ny cell matrix to 1*ny if dim=1 and nx*1 if dim=2 by merging the cells.
*/
X(cell)* X(cellreduce)(const X(cell)* A, int dim){
	if(!A) return NULL;
	X(cell)* out=NULL;
	long nx, ny, * nxs, * nys;
	celldim(CELL(A), &nx, &ny, &nxs, &nys);
	if(nx==0||ny==0) return NULL;
	if(dim==1){
		out=X(cellnew)(1, A->ny);
		for(long iy=0; iy<A->ny; iy++){
			if(nys[iy]==0) continue;
			P(out, iy)=X(new)(nx, nys[iy]);
			for(long icol=0; icol<nys[iy]; icol++){
				long kr=0;
				for(long ix=0; ix<A->nx; ix++){
					if(!isempty(P(A, ix, iy))){
						memcpy(&P(P(out, iy), kr, icol), PCOL(P(A, ix, iy), icol),
							nxs[ix]*sizeof(T));
					}
					kr+=nxs[ix];
				}
			}
		}
	} else if(dim==2){
		out=X(cellnew)(A->nx, 1);
		for(long ix=0; ix<A->nx; ix++){
			if(nxs[ix]==0) continue;
			P(out, ix)=X(new)(nxs[ix], ny);
			long kr=0;
			for(long iy=0; iy<A->ny; iy++){
				if(!isempty(P(A, ix, iy))){
					memcpy(PCOL(P(out, ix), kr), P(P(A, ix, iy)), nxs[ix]*nys[iy]*sizeof(T));
				}
				kr+=nys[iy];
			}
		}
	} else{
		error("Invalid dim=%d\n", dim);
	}
	free(nxs);
	free(nys);
	return out;
}

/**
   drop empty rows or columns. size of *A0 is changed.
*/
void X(celldropempty)(X(cell)** A0, int dim){
	if(!A0) return;
	X(cell)* A=*A0;
	if(!A) return;
	if(dim==1){
		/*drop rows */
		int keep[A->nx];
		int ndrop=0;
		for(int ix=0; ix<A->nx; ix++){
			keep[ix]=0;
			for(int iy=0; iy<A->ny; iy++){
				if(!isempty(P(A, ix, iy))){
					keep[ix]=1;
					break;
				}
			}
			if(keep[ix]==0){
				ndrop++;
			}
		}
		if(ndrop){
			if(ndrop==A->nx){
				X(cellfree)(A);
				*A0=NULL;
			} else{
				X(cell)* B=X(cellnew)(A->nx-ndrop, A->ny);
				int count=0;
				for(int ix=0; ix<A->nx; ix++){
					if(keep[ix]){
						if(count!=ix){
							for(int iy=0; iy<A->ny; iy++){
								P(B, count, iy)=P(A, ix, iy);
							}
						}
						count++;
					} else{
						warning("row %d dropped\n", ix);
					}
				}
				free(P(A)); free(A);
				A=B;
			}
		}
	} else if(dim==2){
	   /*drop cols */
		int count=0;
		for(int iy=0; iy<A->ny; iy++){
			int keep=0;
			for(int ix=0; ix<A->nx; ix++){
				if(!isempty(P(A, ix, iy))){
					keep=1;
					break;
				}
			}
			if(keep){
				for(int ix=0; ix<A->nx; ix++){
					P(A, ix, count)=P(A, ix, iy);
				}
				count++;
			} else{
		   /*warning("Col %d dropped\n", iy); */
			}
		}
		A->ny=count;
		if(count==0){
			X(cellfree)(A);
			*A0=NULL;
		} else{
			P(A)=myrealloc(P(A), A->ny*A->nx, X(mat)*);
		}
	} else{
		error("Invalid dim: %d\n", dim);
	}

}


/**
   convert a vector to cell using dimensions specified in dims. Reference the vector
*/
X(cell)* X(2cellref)(const X(mat)* A, long* dims, long ndim){
	long nx=0;
	for(int ix=0; ix<ndim; ix++){
		nx+=dims[ix];
	}
	if(A->ny!=1){
		error("Use d2cell2 instead for non vectors\n");
	}
	if(nx!=A->nx){
		error("Shape doesn't agree. nx=%ld, nx=%ld\n", nx, A->nx);
	}
	long kr=0;
	X(cell)* B=X(cellnew)(ndim, 1);
	B->m=X(ref)(A);
	for(long ix=0; ix<ndim; ix++){
		P(B, ix)=X(new_do)(dims[ix], 1, P(A)+kr, A->mem);
		kr+=dims[ix];
	}
	return B;
}

/**
   make A a cell array using shape information from ref if *B is NULL.

   Notice that empty cells may be replaced by zeros if it is within valid column or row.
*/
void X(2cell)(X(cell)** B, const X(mat)* A, const X(cell)* ref){
	long nx, ny, * nxs, * nys;
	if(*B) ref=*B;/*use B as reference. */
	celldim(CELL(ref), &nx, &ny, &nxs, &nys);
	if(!A){
		if(nx!=0||ny!=0){
			error("Shape doesn't agree. A is 0x0. Reference is %ldx%ld\n",
				nx, ny);
		}
	} else if(nx!=A->nx||ny!=A->ny){
		error("Shape doesn't agree. Reference is %ldx%ld but input is %ldx%ld\n",
			nx, ny, A->nx, A->ny);
	}
	if(!*B){
		*B=X(cellnew)(ref->nx, ref->ny);
	}
	X(cell)* Bp=(*B);
	for(long iy=0; iy<ref->ny; iy++){
		for(long ix=0; ix<ref->nx; ix++){
			if(nxs[ix]&&nys[iy]&&!P(Bp, ix, iy)){
				P(Bp, ix, iy)=X(new)(nxs[ix], nys[iy]);
			}
		}
	}
	
	long jcol=0;
	for(long iy=0; iy<ref->ny; iy++){
		for(long icol=0; icol<nys[iy]; icol++){
			long kr=0;
			for(long ix=0; ix<ref->nx; ix++){
				if(nxs[ix]){
					memcpy(PCOL(P(Bp, ix, iy), icol), &P(A, kr, icol+jcol), nxs[ix]*sizeof(T));
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
   Create a new sub cell matrix of nx*ny starting from(sx,sy)
*/
X(cell)* X(cellsub)(const X(cell)* in, long sx, long nx, long sy, long ny){
	if(!in) return NULL;
	if(nx<=0){
		nx=in->nx-sx;
	}
	if(ny<=0){
		ny=in->ny-sy;
	}
	X(cell)* out=X(cellnew)(nx, ny);
	if(sx+nx>in->nx||sy+ny>in->ny){
		error("Invalid parameter range\n");
	}
	for(int iy=0; iy<ny; iy++){
		for(int ix=0; ix<nx; ix++){
			P(out, ix, iy)=X(ref)(P(in, ix+sx, iy+sy));
		}
	}
	return out;
}

/**
   input is nsa*ncol cell. each cell has npix=nx*ny elements. Extract icol of cell as npix*nsa array.
*/
X(mat)* X(cell_col)(X(cell)* input, long icol){
	if(!input) return NULL;
	if(icol>NY(input)){
		error("Column %ld exceeds number of columns %ld\n", icol, NY(input));
	}
	long npix=PN(input, 0, 0);
	long nsa=input->nx;
	if(input->m){
		if(PN(input->m)!=npix*PN(input)){
			error("dimension mismatch.\n");
		}
		return X(new_do)(npix, nsa, P(input->m)+(npix*nsa)*icol, NULL);
	} else{
		dbg("Consider using m to speed up\n");
		X(mat)* output=X(new)(npix, nsa);
		for(long isa=0; isa<nsa; isa++){
			if(PN(input, isa, icol)!=npix){
				error("dimension mismatch.\n");
			}
			memcpy(PCOL(output, isa), P(P(input, isa, icol)), sizeof(T)*npix);
		}
		return output;
	}
}
/**
   create sum of all the elements in A.
*/
T X(cellsum)(const X(cell)* A){
	if(isempty(A)) return 0;
	T v=0;
	for(long i=0; i<A->nx*A->ny; i++){
		v+=X(sum)(P(A, i));
	}
	return v;
}
/**
   return sum of each cell in a X(mat)
*/
X(mat)* X(cellsum_each)(const X(cell)* A){
	if(isempty(A)) return 0;
	X(mat) *B=X(new)(NX(A), NY(A));
	for(long i=0; i<PN(A); i++){
		P(B,i)+=X(sum)(P(A, i));
	}
	return B;
}
/**
   Compute the hashlittle
 */
uint32_t X(cellhash)(const X(cell)* A, uint32_t key){
	if(iscell(A)){
		for(long i=0; i<PN(A); i++){
			key=X(hash)(P(A,i), key);
		}
	}
	return key;
}