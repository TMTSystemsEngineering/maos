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
#ifndef AOS_LIB_PCG_H
#define AOS_LIB_PCG_H
#include "../math/mathdef.h"

/**
   \file lib/pcg.h
   Implements preconditioned conjugate gradient method.
*/
typedef void (*cgfun_t) (dcell **xout, const void *A, const dcell *xin, const real alpha);
typedef void (*prefun_t) (dcell **xout, const void *A, const dcell *xin);
real pcg(dcell **px, cgfun_t Amul, const void *A, prefun_t Mmul, const void *M, const dcell *b, 
	   int warm, int maxiter);
#endif
