/*
 *  Hilfsroutinen zum Assembler von tputuner
 *
 *  (c) copyright 1998,1999,2000 by Stefan Reuther
 */
#include <string>
#include "assemble.h"
#include "insn.h"
#include "codewriter.h"

/*
 *  Assembliert einen Arithmetikbefehl
 *  p = Befehl
 *  i = OP-Code
 */
void assemble_arit(CCodeWriter& w, CInstruction* p, int i)
{
    if(p->args[0]->type==CArgument::REGISTER
       && p->args[1]->type==CArgument::IMMEDIATE) {
	/* op AL,ib */
	if(p->args[0]->reg == rAL) {
	    w.wb(4 + 8*i);
	    w.wb(p->args[1]->immediate);
	    return;
	}
	/* op AX,ib */
	if(p->args[0]->reg == rAX) {
	    w.wb(5 + 8*i);
	    p->args[1]->write_immed(w);
	    return;
	}
    }
    if(p->args[1]->is_rm()) {
	/* op r8,rm8 */
	if(p->args[0]->is_byte_reg()) {
	    p->args[1]->write_rm(w, 2 + 8*i, reg_values[p->args[0]->reg]);
	    return;
	}
	/* op r16,rm16 */
	if(p->args[0]->is_word_reg()) {
	    p->args[1]->write_rm(w, 3 + 8*i, reg_values[p->args[0]->reg]);
	    return;
	}
    }
    if(p->args[0]->is_rm()) {
	/* op rm8,r8 */
	if(p->args[1]->is_byte_reg()) {
	    p->args[0]->write_rm(w, 0 + 8*i, reg_values[p->args[1]->reg]);
	    return;
	}
	/* op rm16,r16 */
	if(p->args[1]->is_word_reg()) {
	    p->args[0]->write_rm(w, 1 + 8*i, reg_values[p->args[1]->reg]);
	    return;
	}

	if(p->args[1]->type == CArgument::IMMEDIATE) {
	    /* op rm8,ib */
	    if(p->byte_insn()) {
		p->args[0]->write_rm(w, 0x80, i);
		w.wb(p->args[1]->immediate);
		return;
	    }
	    /* op rm16,iw */
	    if(!p->args[1]->is_byte_imm()) {
		p->args[0]->write_rm(w, 0x81, i);
		p->args[1]->write_immed(w);
		return;
	    }
	    /* op rm16,ib */
	    else {
		p->args[0]->write_rm(w, 0x83, i);
		w.wb(p->args[1]->immediate);
		return;
	    }
	}
    }
    throw string("Can't assemble aritmetic instruction");
}

/*
 *  Assembliert Shift-Befehl
 *  p = Befehl
 *  i = OP-Code
 */
void assemble_shift(CCodeWriter& w, CInstruction* p, int i)
{
    if(p->args[1]->is_byte_reg()) {
	if(p->args[1]->reg != rCL)
	    throw string("Shift with other register than CL");
	/* op rm,CL */
	p->args[0]->write_rm(w, p->byte_insn()?0xD2:0xD3, i);
    } else if(p->args[1]->type == CArgument::IMMEDIATE) {
	if(p->args[1]->immediate == 1) {
	    /* op rm,1 */
	    p->args[0]->write_rm(w, p->byte_insn()?0xD0:0xD1, i);
	} else {
	    /* op rm,ib */
	    p->args[0]->write_rm(w, p->byte_insn()?0xC0:0xC1, i);
	    w.wb(p->args[1]->immediate);
	}
    } else throw string("Invalid shift insn");
}
