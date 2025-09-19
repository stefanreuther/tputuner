/*
 *  Optimierer für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include "global.h"

struct TFuncName {
    char* unit;
    char* func;
};

CNewCode* do_optimize(int id,
                      char* code, int code_size, int my_ip,
                      char* relo, int relo_count, CEntryBlock* entry);

extern char* global_code_ptr;
extern int global_code_id;
extern bool changed;
extern bool can_remove_stackframe;
extern int excluded_func_num;
extern TFuncName* excluded_funcs;
extern char* unit_name;

#endif
