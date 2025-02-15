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
#ifndef AOS_LIB_PATH_H
#define AOS_LIB_PATH_H
/**
   \file path.h
   
   This file contains function managing the searching path for files, behaves
   like PATH in POSIX systems.
*/
void addpath2(int priority, const char *format, ...) CHECK_ARG(2);
void addpath(const char *path);
void rmpath(const char *path);
void printpath(void);
char *search_file(const char *fn, int current);/*not quit if not found */
void freepath(void);
char *find_config(const char *name);
#endif
