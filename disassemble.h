/*
 *  Disassembler für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#ifndef DISASM_H
#define DISASM_H

#include "insn.h"

CInstruction* disassemble(char* acode, int code_size, int my_offset,
			  char* _relo, int _relo_count);

#endif
