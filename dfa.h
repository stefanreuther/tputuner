/*
 *  "Datenflußanalyse" für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#ifndef DFA_H
#define DFA_H

#include "insn.h"

void data_flow_analysis(CInstruction* insn);
bool flag_check(CInstruction* insn);
bool can_modify_reg(CInstruction* p, TRegister r);
void make_backlinks(CInstruction* insn);

#endif
