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
#ifndef AOS_LIB_MKH_H
#define AOS_LIB_MKH_H
#include "../math/mathdef.h"
/**
   \file mkh.h
   Contains functions that create ray tracing operator
*/
dsp *mkht_cubic(const loc_t *locin, const loc_t *locout, real displacex, real displacey, real scale, real angle, real iac);
dsp *mkh_cubic(const loc_t *locin, const loc_t *locout, real displacex, real displacey, real scale, real angle, real iac);
dsp *mkht(const loc_t *locin, const loc_t *locout, real displacex, real displacey, real scale, real angle);
dsp *mkh(const loc_t *locin, const loc_t *locout, real displacex, real displacey,real scale, real angle);
dsp *mkhbin1d(const dmat *xin, const dmat *xout);
#endif
