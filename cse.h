#ifndef CSE_H
#define CSE_H

#include <iosfwd>
#include <vector>
#include "insn.h"

struct OperandSet {
    int regs;
    vector<CArgument*> mem;
    bool flags, stack;
    
    OperandSet() : regs(0), flags(false), stack(false) { }
    void fix_regs();
    void fix_regs_conservative();
    void add_op(CArgument* a) { add_op(a, *this); }
    void add_op(CArgument* a, OperandSet& read);
    bool contains_arg(CArgument* a);
};

std::ostream& operator<<(std::ostream& os, const OperandSet& set);

void do_cse(CInstruction* insn);
void compute_insn_dep(OperandSet& in, OperandSet& out, CInstruction* p);
bool is_break(CInstruction* p);

#endif
