/*
 *  Optimierer für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include "global.h"

CNewCode* do_optimize(int id,
		      char* code, int code_size, int my_ip,
		      char* relo, int relo_coun);

extern char* global_code_ptr;
extern int global_code_id;
extern bool changed;

#endif
