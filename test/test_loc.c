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
#include "../lib/aos.h"
/*
static void test_w1(){
    loc_t *loc=mksqloc2(200,200,1./64.);
    real *amp=mycalloc(loc->nloc,real);
    loc_circle_add(amp,loc,0,0, 1,1);
    drawopd("test_loc",loc,amp,"loc");
    normalize(amp,loc->nloc,1);
    dsp *W0=NULL;
    dmat *W1=NULL;
    mkw_amp(loc,amp,&W0,&W1);
    dbg("Sum W1=%g",dsum(W1));
    writedbl(amp,loc->nloc,1,"amp");
    writebin(W1,"W1");
    writebin(W0,"W0");
    locwrite(loc,"loc");
}
static void test_wploc(){
    loc_t *loc=locread("ploc.bin");
    real *amp=NULL; long nx,ny;
    readdbl(&amp,&nx,&ny,"pamp.bin");
    real ampmax=maxdbl(amp,nx);
    real ampmax1=1./ampmax;
    for(int i=0; i<nx; i++){
	amp[i]=round(amp[i]*ampmax1)*ampmax;
    }
    normalize(amp,nx,1);
    drawopd("test_loc",loc,amp,"loc");
    normalize(amp,loc->nloc,1);
    dsp *W0=NULL;
    dmat *W1=NULL;
    mkw_amp(loc,amp,&W0,&W1);
    dbg("Sum W1=%g\n",dsum(W1));
    writedbl(amp,loc->nloc,1,"amp");
    writebin(W1,"pW1");
    writebin(W0,"pW0");
}
static void test_wcir(){
    loc_t *loc=mksqloc2(100,100,1./4.);
    dsp *W0=NULL;
    dmat *W1=NULL;
    mkw_circular(loc,0,0,10,&W0,&W1);
    locwrite(loc,"loc");
    writebin(W0,"W0");
    writebin(W1,"W1");
}
static void test_int_reg(){
    int n=100;
    real dx=1./n;
    real cy=0;
    real cx=0;
    real cr=0.3;
    real cr2=cr*cr;
    int area=0;
    for(int i=0; i<n; i++){
	real y=dx*(i+0.5);
	real r2y=pow(y-cy,2);
	for(int j=0; j<n; j++){
	    real x=dx*(j+0.5);
	    if(pow(x-cx,2)+r2y < cr2){
		area++;
	    }
	}
    }
    real ar=(real)area/(real)(n*n);
    dbg("Area=%g, Err=%g\n",ar,ar-M_PI*cr2/4);
}
static void test_int_rand(){
    int n=10000;
    real cy=0;
    real cx=0;
    real cr=0.3;
    real cr2=cr*cr;
    int area=0;
    rand_t stat;
    seed_rand(&stat,1);
    for(int i=0; i<n; i++){
	real x=drand48();
	real y=drand48();
	if(pow(x-cx,2)+pow(y-cy,2) <= cr2){
	    area++;
	}
    } 
    real ar=(real)area/(real)(n);
    dbg("Area=%g, Err=%g\n",ar,ar-M_PI*cr2/4);
}
*/
/*
static void test_sqlocrot(void){
    int nn=64;
    real dx=1./64.;
    loc_t *loc=mksqlocrot(nn,nn,dx,-nn/2*dx,-nn/2*dx,0);
    locwrite(loc,"loc_0deg.bin");
    loc_t *loc2=mksqlocrot(nn,nn,dx,-nn/2*dx,-nn/2*dx,M_PI/10);
    locwrite(loc2,"loc_18deg.bin");
    }*/
/*
void test_loc_reduce_sp(void){
    int nloc;
    loc_t **xloc=locarrread(&nloc,"xloc.bin");
    dspcell *G0=dspcellread("G0.bin");
    loc_reduce_sp(xloc[0],P(G0,0),2,1);
    locarrwrite(xloc,nloc,"xloc2");
    writebin(G0,"G02.bin");
    locarrfree(xloc,nloc);
    dspcellfree(G0);
    G0=dspcellread("G0.bin");
    xloc=locarrread(&nloc,"xloc.bin");
    dsp *G0t=dsptrans(P(G0,0));
    loc_reduce_sp(xloc[0],G0t,1,1);
    dsp *G03=dsptrans(G0t);
    writebin(G03,"G03.bin");
    locarrwrite(xloc,nloc,"xloc3");
    }

static void test_loc_reduce_spcell(void){
    int nloc;
    loc_t **xloc=locarrread(&nloc,"xloc.bin");
    dspcell *G0=dspcellread("G0.bin");
    dspcell *G0t=dspcelltrans(G0);
    loc_reduce_spcell(xloc[0],G0t,1,1);
    dspcell *G04=dspcelltrans(G0t);
    writebin(G04,"G05.bin");
    locarrwrite(xloc,nloc,"xloc5");
    }
static void test_zernike(){
    loc_t *loc=locread("loc_circle.bin");
    dmat *mod=zernike(loc,1,0, 10, 0);
    locwrite(loc,"loc");
    writebin(mod,"mod");
    dgramschmidt(mod,NULL);
    writebin(mod,"mod_norm");
    }
static void test_embed(){
    loc_t *loc=mkannloc(10, 0, 1./64,0);
    locwrite(loc, "loccir");
    dmat *opd=dnew(645,645);
    dembed_locstat(&opd, loc, NULL);
    writebin(opd, "locopd");
    }*/
static void test_parse_poly(const char *str){
    dmat *c=parse_poly(str);
    dshow(c, "parsed");
    dfree(c);
}
int main(int argc, const char *argv[]){
    /*  
    test_zernike();
    test_loc_reduce_spcell();
    test_sqlocrot();
	test_w1();
    test_wploc();
    test_wcir();
    test_int_reg();
    test_int_rand();
    test_embed();
    
    for(long i=1; i<1000;i++){
	dbg("i=%ld, %ld\n", i, nextpow2(i));
    }*/
    if(argc>1){
        test_parse_poly(argv[1]);
    }
}
