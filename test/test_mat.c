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
#include <sys/mman.h>
#include "../lib/aos.h"

TIC;

static void test_clip(){
    int N=64;
    dmat *A=dnew(N,N);
    rand_t rstat;
    seed_rand(&rstat,1);
    drandn(A,1,&rstat);
    //writebin(A,"A1");
    dclip(A,-0.5, 0.5);
    //writebin(A,"A2");
    /*exit(0); */
}
static void test_hist(){
    int N=64;
    dmat *A=dnew(N,N);
    dmat *B=NULL;
    rand_t rstat;
    seed_rand(&rstat,1);
    for(int i=0; i<100; i++){
	drandn(A, 1, &rstat);
	dhistfill(&B, A, 0, 0.1, 100);
    }
    //writebin(B,"hist");
    /*exit(0); */
}
static void test_dcellpinv(){
    dcell *TT=dcellread("TT.bin");
    dspcell *saneai=dspcellread("saneai.bin");
	dcell *PTT=dcellpinv(TT, saneai);
    //writebin(PTT,"TT_pinv.bin");
    dcell *PTT2=dcellnew2(TT);
    for(int i=0; i<PTT->ny; i++){
	    P(PTT2,i,i)=dpinv(P(TT,i,i),P(saneai,i,i));
    }
    //writebin(PTT2,"TT2_pinv.bin");
    /*exit(0); */
}
static void test_dcellcat(int argc, char**argv){
    if(argc!=2){
	error("Usage: %s FILE.bin", argv[0]);
    }
    dcell *A=dcellread("%s",argv[1]);
    dcell *B=dcellreduce(A,1);
    dcell *C=dcellreduce(A,2);
    writebin(B,"B");
    writebin(C,"C");
    exit(0);
}
static void test_save(void){/*passed */
    /*dbg("page size is %ld\n",sysconf(_SC_PAGE_SIZE)); */
    dmat *a=dnew_mmap(20,20,NULL,"a");
    rand_t rstat;
    seed_rand(&rstat,1);
    drandn(a,1,&rstat);
    //writebin(a,"a2.bin");
    long nnx[6]={2,3,4,5,6,0};
    long nny[6]={3,4,2,5,6,3};
    dcell *b=dcellnew_mmap(2,3, nnx, nny, NULL,"ac.bin");
    for(int ix=0; ix<b->nx*b->ny; ix++){
	drandn(P(b,ix), 1, &rstat);
    }
    //writebin(b, "ac2.bin");
    dcellfree(b);
    dfree(a);
    exit(0);
}
static void test_spline(void){
    long nx=20;
    dmat *x=dnew(nx,1);
    dmat *y=dnew(nx,1);
    dmat *x2=dnew(nx*100,1);
    for(int i=0; i<nx; i++){
	P(x,i)=i;
	P(y,i)=sin(i*M_PI/2.);
    }
    for(int i=0; i<nx*100; i++){
	P(x2,i)=0.01*i;
    }
    dmat *coeff=dspline_prep(x,y);
    dmat *y2=dspline_eval(coeff,x,x2);
    dmat *y3=dspline(x,y,x2);
    //writebin(x,"x");
    //writebin(y,"y");
    //writebin(x2,"x2");
    writebin(y2,"y2");
    writebin(y3,"y3");
    //writebin(coeff,"coeff");
    exit(0);
}
static void test_spline_2d(void){
    long nx=20;
    long ny=30;
    dmat *x=dnew(nx,1);
    dmat *y=dnew(ny,1);
    dmat *z=dnew(nx,ny);
    for(int i=0; i<ny; i++){
	P(y,i)=i*1.2+10;
    }
    for(int i=0; i<nx; i++){
	P(x,i)=i*2+10;
    }
    dmat* pz=z;
    for(int iy=0; iy<ny; iy++){
	for(int ix=0; ix<nx; ix++){
	    P(pz,ix,iy)=sin(ix*M_PI/14.2)*sin(iy*M_PI/12.3);
	}
    }
    long nxnew=nx*11;
    long nynew=ny*11;
    dmat *xnew=dnew(nxnew,nynew);
    dmat *ynew=dnew(nxnew,nynew);
    dmat* pxnew=xnew;
    dmat* pynew=ynew;
    for(int iy=0; iy<nynew; iy++){
	for(int ix=0; ix<nxnew; ix++){
	    P(pxnew,ix,iy)=ix*0.2+6;
	    P(pynew,ix,iy)=iy*0.12+6;
	}
    }
    dcell *coeff=dbspline_prep(x,y,z);
    dmat *z22=dbspline_eval(coeff,x,y,xnew,ynew);

    //writebin(x,"x");
    //writebin(y,"y");
    //writebin(z,"z");

    //writebin(xnew,"xnew");
    //writebin(ynew,"ynew");
    writebin(z22,"z22");
    //writebin(coeff,"coeff");
    exit(0);
}
static void test_svd(void){
    dspcell *a=dspcellread("SVD");
    dmat *A=NULL;
    dspfull(&A, P(a,0), 'n',1);
    if(0){
	dmat *U, *S, *VT;
	tic;
	dsvd(&U, &S, &VT, A);
	toc("dsvd");
	tic;
	//writebin(U,"U.bin");
	//writebin(S, "S.bin");
	//writebin(VT,"VT.bin");
    }else{
	//writebin(A,"A.bin");
	tic;
	dsvd_pow(A, -1);
	toc("dsvd_pow");
	//writebin(A,"AI.bin");
	tic;
	dsvd_pow(A, -1);
	toc("dsvd_pow svd");
	//writebin(A,"A2.bin");
    }
    exit(0);
}
static void test_psd1d(){
    dmat *tmp=dnew(100,1);
    for(int i=0; i<100; i++){
	P(tmp,i)=sin(i/10.);
    }
    dmat *psd=psd1d(tmp, 1);
    //writebin(tmp, "tmp");
    writebin(psd, "psd");
    exit(0);
}
static void test_svd2(void){
    cmat *A=cread("SVD.bin");
    csvd_pow(A, -1);
    //writebin(A, "SVDI");
}
static void test_kalman(){
    //dmat *psd=dread("MODE_TT");
    dbg("sde_fit\n");
    //dmat *coeff0=dread("coeff0");
    //dmat *coeff=sde_fit(psd, coeff0, 0.1, 0, 1e5, 0);
    dmat *coeff=dread("coeff");
    //dmat *Gwfs=dnew(1,1);daddI(Gwfs,1);
    //dmat *Rwfs=dnew(1,1);P(Rwfs,0)=1;
    dcell *Gwfs=dcellread("Gwfs");
    dcell *Rwfs=dcellread("Rwfs");
    lmat *dtrat_wfs=lread("dtrat_wfs");
    dmat *proj=dread("Proj");
    kalman_t *k=sde_kalman(coeff, 1./800, dtrat_wfs, NULL, Gwfs, Rwfs, proj);
    //writebin(k->Ad, "mex_Ad");
    //writebin(k->Cd, "mex_Cd");
    //writebin(k->M, "mex_M");
    writebin(k->P, "mex_P");
    /*rand_t rstat; seed_rand(&rstat, 1);
    //dmat *ts=psd2ts(psd, &rstat, 1./800, 5000);
    dmat *ts=dread("ts");
    for(int i=0; i<100; i++){
	dmat *res=kalman_test(k, ts);
	//writebin(res, "kalman_res");
	dfree(res);
	}*/
    exit(0);
}
/*static void test_reccati(){
    dmat *A=dread("test_A");
    dmat *Qn=dread("test_Qn");
    dmat *C=dread("test_C");
    dmat *Rn=dread("test_Rn");
    dmat *M=0, *P=0;
    M=reccati(&P, A, Qn, C, Rn);
    //writebin(M, "test_M");
    //writebin(P, "test_P");
    exit(0);
}*/
static void test_expm(){
    dmat *A=dread("expm_in");
    dmat *B=0;
    dexpm(&B, 1, A, 1);
    //writebin(B, "expm_out");
    exit(0);
}
static void test_mm(){
    dcell *A=dcellnew(1,1);
    dcell *B=dcellnew(1,1);
    P(A,0)=dnew(3,4);
    dset(P(A,0),1);
    P(B,0)=dnew(4,5);
    dset(P(B,0),1);
    dcell *C=NULL;
	dcellmm(&C, A, B, "nn", 1);
	dcellmm(&C, P(A,0), B, "nn", 1);
	dcellmm(&C, P(A,0), P(B,0), "nn", 1);
	dmat *C2=NULL;
	dcellmm(&C2, P(A,0), B, "nn", 1);
	dcellmm(&C2, P(A,0), P(B,0), "nn", 1);
    //writebin(A, "A");
    //writebin(B, "B");
    //writebin(C, "C");

    exit(0);
}
void test_sho(){
    dmat *x=dread("input");
    //dmat *y=dnew(x->nx, x->ny);
    //sho_t *sho=sho_new(200, 0.9);
    real f0=200;
    real dt=1./64000.;
    real zeta=0.9;
    dmat *y=sho_filter(x, dt, f0, zeta, 0);
    //writebin(y, "output");
    dmat *x2=dread("input2");
    real dt2=1./800.;
    dmat *y2=sho_filter(x2, dt2, f0, zeta, 0);

    //writebin(y2, "output2");
    dfree(x);
    dfree(y);
    dfree(x2);
    dfree(y2);
}
void test_sde(){
    if(zfexist("psd_in.bin")){
	dmat *psd=dread("psd_in.bin");
	dmat *coeff=dread("coeff_in.bin");
	sde_fit(&coeff, psd, 0.1, 0);
	writebin(coeff, "sde_res.bin");
    }
    exit(0);
}
void test_async(){
    dmat*xx=dnew_file(10,10,"test_mat","test_mat.bin");
    
    long sizes[3]={10,0,10};
    dcell* xc=dcellnew_file(3, 1, sizes, sizes, "test0", "test_cell.bin");
    for(int i=0; i<PN(xc); i++){
        dmat *x=P(xc,i);
        //x->offset=-1;//turn on async
        //writebin_auto(x);
    
        for(long iy=0; iy<NY(x); iy++){
            for(long ix=0; ix<NX(x); ix++){
                P(x, ix, iy)=ix+iy*10;
            }
        }
    }
    dcp(&xx, P(xc,0));
    
    writebin_async(xx, 2);
    dfree(xx);
    //writebin(xc, "test_cell0.bin");

    writebin_async(xc, 1);
    writebin_async(xc, 3);

    //writebin_async(xc, 800);
    
   
    dcellfree(xc);
    exit(0);
}
void test_servo(){
    real g=servo_optim_margin(1/600., 1, 1, M_PI/4, 0, 0);
    info("gain=%g\n", g);
    exit(0);
}

int main(int argc, char **argv){
    test_servo();
    test_async();
    test_sde();
    test_sho();
    test_mm();
    test_expm();
    //test_reccati();
    test_kalman();
    test_svd2();
    test_svd();
    test_psd1d();
    test_spline_2d();
    test_spline();

    test_save();
    test_dcellcat(argc, argv);
    test_dcellpinv();
    test_hist();
    test_clip();
   
}
