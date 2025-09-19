/*
 *  Hilfsroutinen zum Assembler von tputuner
 *
 *  (c) copyright 1998,1999,2000 by Stefan Reuther
 */
#include <string>
#include "assemble.h"
#include "insn.h"
#include "codewriter.h"
#include "global.h"

using std::string;

/*
 *  Assembliert einen Arithmetikbefehl
 *  p = Befehl
 *  i = OP-Code
 */
void assemble_arit(CCodeWriter& w, CInstruction* p, int i)
{
    /* TEST instruction does not fit entirely into the standard encoding
       of arithmetic instructions */
    bool test_insn=(p->insn==I_TEST);
    if(p->args[0]->type==CArgument::REGISTER
       && p->args[1]->type==CArgument::IMMEDIATE) {
        /* op AL,i8 */
        if(p->args[0]->reg == rAL) {
            if (test_insn)
                i=0xA8;
            else
                i=4 + 8*i;
            w.wb(i);
            w.wb(p->args[1]->immediate);
            return;
        }
        /* op AX,i8 */
        if(p->args[0]->reg == rAX) {
            if (test_insn)
                i=0xA9;
            else
                i=5 + 8*i;
            w.wb(i);
            p->args[1]->write_immed(w);
            return;
        }
    }
    if(p->args[1]->is_rm()) {
        /* op r8,rm8 */
        if(!test_insn && p->args[0]->is_byte_reg()) {
            p->args[1]->write_rm(w, 2 + 8*i, reg_values[p->args[0]->reg]);
            return;
        }
        /* op r16,rm16 */
        if(!test_insn && p->args[0]->is_word_reg()) {
            p->args[1]->write_rm(w, 3 + 8*i, reg_values[p->args[0]->reg]);
            return;
        }
    }
    if(p->args[0]->is_rm()) {
        /* op rm8,r8 */
        if(p->args[1]->is_byte_reg()) {
            if (test_insn)
                i=0x84;
            else
                i=0 + 8*i;
            p->args[0]->write_rm(w, i, reg_values[p->args[1]->reg]);
            return;
        }
        /* op rm16,r16 */
        if(p->args[1]->is_word_reg()) {
            if (test_insn)
                i=0x85;
            else
                i=1 + 8*i;
            p->args[0]->write_rm(w, i, reg_values[p->args[1]->reg]);
            return;
        }

        if(p->args[1]->type == CArgument::IMMEDIATE) {
            /* op rm8,i8 */
            if(p->byte_insn()) {
                p->args[0]->write_rm(w, test_insn?0xF6:0x80, i);
                w.wb(p->args[1]->immediate);
                return;
            }
            /* op rm16,i16 */
            if(test_insn || !p->args[1]->is_byte_imm()) {
                p->args[0]->write_rm(w, test_insn?0xF7:0x81, i);
                p->args[1]->write_immed(w);
                return;
            }
            /* op rm16,i8 */
            else {
                p->args[0]->write_rm(w, 0x83, i);
                w.wb(p->args[1]->immediate);
                return;
            }
        }
    }
    throw string("Can't assemble arithmetic instruction");
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
            /* op rm,i8 */
            p->args[0]->write_rm(w, p->byte_insn()?0xC0:0xC1, i);
            w.wb(p->args[1]->immediate);
        }
    } else throw string("Invalid shift insn");
}
