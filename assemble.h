/*
 *  Hilfsroutinen zum Assembler von tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#ifndef ASSEMBLE_H
#define ASSEMBLE_H

#include "codewriter.h"

void assemble_arit(CCodeWriter& w, CInstruction* p, int i);
void assemble_shift(CCodeWriter& w, CInstruction* p, int i);

#endif
