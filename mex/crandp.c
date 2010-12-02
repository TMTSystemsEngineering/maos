#include "random.h"
#include <mex.h>
#include <math.h>
#define USE_PTHREAD 0

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    rand_t *strand;
    double *p,*pin;
    int nx,ny;
    int i;
    if(nrhs!=2) mexErrMsgTxt("Except two input\n");
    strand=(rand_t*)mxGetPr(prhs[0]);
    nx=mxGetM(prhs[1]);
    ny=mxGetN(prhs[1]);
    pin=mxGetPr(prhs[1]);
    plhs[0]=mxCreateDoubleMatrix(nx,ny,mxREAL);
    p=mxGetPr(plhs[0]);
    for(i=0; i<nx*ny; i++){
	p[i]=(double)randp(strand,pin[i]);
    }
}