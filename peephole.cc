/*
 *  Peephole-Optimierung für tputuner
 *
 *  (c) copyright 1998,1999,2000 by Stefan Reuther
 *
 *  Ruft jede der Funktionen aus /functions/ für jede Anweisung
 *  einzeln auf. Rückgabe ist
 *  - A_BAD      Funktion kann mit Anweisung nichts anfangen
 *  - A_CONTINUE (nicht für Optimizer-Funktionen)
 *  - A_DELETE   Anweisung kann gelöscht werden
 *  - A_RESCAN   Anweisung wurde optimiert, noch mal bewerten
 */
#include "insn.h"
#include "peephole.h"
#include "dfa.h" /* für flag_check() */
#include "optimize.h"
#include "global.h"
#include "tpufmt.h"
#include "cse.h" /* für compute_insn_dep() */

#define ALL_SET(x,y) (((x) & (y)) == (y))

extern int sys_unit_offset;     // in tputuner.cc
extern int ref_this_unit;       // in tputuner.cc

typedef enum {
    A_BAD,         // Funktion paßt nicht auf insn
    A_CONTINUE,    // keine Änderung, trotzdem weitermachen
    A_DELETE,      // Befehl löschen
    A_RESCAN       // geändert, nochmal bewerten
} TAction;

/* Sprung Charakteristika */
int jmp_char[] = {
    IF_OTHER,
    IF_OTHER,
    IF_BELOW,
    IF_ABOVE | IF_EQUAL,
    IF_EQUAL,
    IF_NONEQUAL,                // nur jne darf IF_NONEQUAL haben
    IF_BELOW | IF_EQUAL,
    IF_ABOVE,
    IF_OTHER,
    IF_OTHER,
    IF_OTHER,
    IF_OTHER,
    IF_LESS,
    IF_GREATER | IF_EQUAL,
    IF_LESS | IF_EQUAL,
    IF_GREATER
};

int jmp_for_char (int ch)
{
    for (int i = 0; i < 16; ++i)
        if (jmp_char[i] == ch)
            return i;
    return -1;
}

/*
 *  Prüft Nullbefehle
 */
TAction check_nop(CInstruction* p)
{
    if(p->insn == I_LABEL && p->param == 0)
        return A_DELETE;   /* label`0'  (Ohne Referenz) */
    if((p->insn==I_MOV || p->insn==I_XCHG)
       && *p->args[0]==*p->args[1])
        return A_DELETE;   /* Selbstzuweisung */
    if((p->insn==I_JCC || p->insn==I_JCXZ || p->insn==I_JMPN)
       && p->args[0]->type==CArgument::LABEL
       && p->args[0]->label==p->next)
        /* jmp $+2 in allen Geschmacksrichtungen */
        return A_DELETE;
    
    return A_BAD;
}

/*
 *  Inverse Conditional Jumps
 */
TAction check_jump(CInstruction* p)
{
    /* jcc <label:> */
    if(p->insn != I_JCC || p->args[0]->type != CArgument::LABEL)
        return A_BAD;

    /* jmp <woanders> */
    if(!p->next || !p->next->args[0] || p->next->args[0]->type != CArgument::LABEL)
        return A_BAD;

    if(p->next->insn == I_JMPN) {
        if(p->args[0]->label == p->next->next) {
            /* inverse conditional jump */
            CInstruction* n = p->next;
            n->insn = I_JCC;
            n->param = p->param ^ 1;
            return A_DELETE;
        } else if(*p->args[0] == *p->next->args[0]) {
            /* Zwei Sprünge - eine Meinung */
            return A_DELETE;
        }
    } else if(p->next->insn == I_JCC) {
        CInstruction* n = p->next;

        /* Zwei Sprnge. Wenn der zweite immer dann springt, wenn
           der erste das auch tut -> l”sche zweiten */
        int fst = jmp_char[p->param];
        int snd = jmp_char[n->param];
        if (fst == IF_NONEQUAL) 
            fst = IF_ABOVE | IF_BELOW | IF_LESS | IF_GREATER;
        if (snd == IF_NONEQUAL) 
            snd = IF_ABOVE | IF_BELOW | IF_LESS | IF_GREATER;
        if (((fst | snd) & IF_OTHER) == 0 && ALL_SET(fst, snd)) {
            /* trifft zu */
            p->next = n->next;
            delete n;
            return A_RESCAN;
        }

        if (*p->args[0] == *n->args[0]) {
            /* Zwei Sprnge zum selben Ziel */
            /* probiere, einen Sprung zu synthetisieren, der
               beides auf einmal kann */
            int when = jmp_char[p->param] | jmp_char[n->param];
            if (when & IF_OTHER)
                return A_BAD;
            if (ALL_SET(when, IF_ABOVE | IF_BELOW | IF_EQUAL)
                || ALL_SET(when, IF_LESS | IF_GREATER | IF_EQUAL)
                || ALL_SET(when, IF_EQUAL | IF_NONEQUAL)) {
                /* jcond     <foo>
                   jnotcond  <foo>    => jmp <foo> */
                n->insn = I_JMPN;
                n->param = 0;
                return A_DELETE;
            }
            if (when & IF_NONEQUAL) {
                /* ja <foo>
                   jne <foo>    => ja <foo> */
                n->param = jmp_for_char(when & ~IF_NONEQUAL);
                return A_DELETE;
            }
            if (ALL_SET(when, IF_LESS | IF_GREATER)
                || ALL_SET(when, IF_ABOVE | IF_BELOW)) {
                /* ja <foo>
                   jb <foo>   => jne <foo> */
                n->param = CC_NE;
                return A_DELETE;
            }
            int x = jmp_for_char(when);
            if (x < 0)
                return A_BAD;
            n->param = x;
            return A_DELETE;
        }
    }
    return A_BAD;
}

/*
 *  Optimiert Vergleiche
 */
TAction check_cmp(CInstruction* p)
{
    if(p->insn == I_CMP && p->args[0]->type==CArgument::REGISTER
       && reg_sizes[p->args[0]->reg]==2
       && p->args[1]->is_immed(0) && p->opsize == 2) {
        /*
         *  cmp(2) reg,0
         *  -> or reg,reg
         */
        delete p->args[1];
        p->args[1] = new CArgument(*(p->args[0]));
        p->insn = I_OR;
        
        return A_RESCAN;
    }
    return A_BAD;
}

/*
 *  Nulltest folgt auf Arithmetikoperation
 */
TAction check_zerotest(CInstruction* p)
{
    if((p->insn==I_ADD || p->insn==I_SUB || p->insn==I_OR
        || p->insn==I_AND || p->insn==I_XOR /*|| p->insn==I_INC
        || p->insn==I_DEC*/)
       /*&& p->args[0]->type==CArgument::REGISTER*/ && p->next) {
        /*
         *  op reg,xxx
         *  nulltest reg
         *  -> den Nulltest löschen
         */
        CInstruction* in = p->next;
        if(in->insn==I_OR || in->insn==I_AND) {
            /* and/or */
            if(/*in->args[0]->type != CArgument::REGISTER
               || in->args[0]->reg != p->args[0]->reg*/
               !(*in->args[0] == *p->args[0])
               || !(*in->args[0]==*in->args[1]))
                return A_BAD;
        } else if(in->insn==I_CMP) {
            /* cmp reg,0 */
            if(/*in->args[0]->type != CArgument::REGISTER
               || in->args[0]->reg != p->args[0]->reg*/
               !(*in->args[0] == *p->args[0])
               || !in->args[1]->is_immed(0))
                return A_BAD;
        } else
            return A_BAD;

        /* wir können den nächsten Befehl wegwerfen */
        p->next = in->next;
        delete in;

        return A_RESCAN;
    }
    return A_BAD;
}

/*
 *  Nulltest gefolgt von `jnb' oder `jb'
 */
TAction check_zerotest_jb(CInstruction* p)
{
    if(((p->insn==I_AND || p->insn==I_OR) && *p->args[0]==*p->args[1])
       || p->insn==I_CMP && p->args[1]->is_immed(0)) {
        CInstruction* n = p->next;
        if(n->insn==I_JCC && (n->param==3 || n->param==2)) {
            /* Nulltest + jb/jnb */
            if(n->param == 3) {
                /* jnb -> jmp */
                n->insn = I_JMPN;
            } else {
                /* jb -> löschen */
                p->next = n->next;
                delete n;
            }
            return A_RESCAN;
        } else if(n->insn==I_JMPN) {
            /* Nulltest + jmp */
            if(flag_check(n->args[0]->label->next))
                /* Nulltest löschen */
                return A_DELETE;
        }
    }
    return A_BAD;
}

/*
 *  add/sub r/m,imm
 *  imm mit speziellen Werten
 */
TAction check_smalladd(CInstruction* p)
{
    if((p->insn!=I_ADD && p->insn!=I_SUB) || p->opsize!=2
       || p->args[1]->type!=CArgument::IMMEDIATE || p->args[1]->reloc!=0)
        return A_BAD;

    if(p->args[1]->immediate==128) {
        /* Operand -128 ist eins kürzer als +128 */
        p->args[1]->immediate=-128;
        if(p->insn==I_ADD)
            p->insn=I_SUB;
        else
            p->insn=I_ADD;
        return A_RESCAN;
    }

    if(!flag_check(p->next)) {
        return A_BAD; // Flags beachten
    }
    if(p->args[1]->immediate==0) return A_DELETE;

    /* können wir optimieren? */
    int delta = p->args[1]->immediate;
    bool increase = (p->insn==I_ADD);

    if(delta<0) {
        increase = !increase;
        delta = -delta;
    }

    if(p->args[0]->is_word_reg()) {
        if(p->args[0]->reg==rSP && !increase
           && (delta==2 || (delta==4 && do_size))) {
            /* sub sp,2  resp.  sub sp,4 */
            p->insn = I_PUSH;
            delete p->args[0];
            delete p->args[1];
            p->args[1]=0;
            p->args[0]=new CArgument(rAX);

            if(delta==4) {
                CInstruction* n = new CInstruction(I_PUSH, new CArgument(rAX));
                n->next = p->next;
                p->next = n;
            }
            return A_RESCAN;
        } else if(delta>2 || (!do_size && delta==2))
            return A_BAD;
    } else if(p->args[0]->type==CArgument::MEMORY) {
        if(delta != 1) return A_BAD;
    }

    TInsn i = increase?I_INC:I_DEC;
    changed = true;
    p->insn = i;
    delete p->args[1];
    p->args[1] = 0;
    while(--delta) {
        CInstruction* n = new CInstruction(i, new CArgument(*p->args[0]));
        n->set_os(p->opsize);
        n->next = p->next;
        p->next = n;
    }
    return A_RESCAN;
}

static int classify_arg(CArgument* l)
{
    if(l->memory[0] != rNONE || l->memory[1] != rNONE || l->segment != rDS)
        return 2;
    else
        if(l->reloc)
            return 1;
        else
            return 0;
}

/*
 *  Kanonische Reihenfolge feststellen
 *
 *  [a1] < [a2]      <=>  a1 < a2
 *  [a1] < [reg+a2]
 *  [reg+a1] < [reg+a2] <=> a1 < a2
 *
 *  /arg_compare/ liefert true, wenn r vor l kommt. reg+imm werden noch nicht verglichen
 */
static bool arg_compare(CArgument* l, CArgument* r)
{
    int t1 = classify_arg(l);
    int t2 = classify_arg(r);

    if(t1 == t2) {
        if(t1 == 2)             // [reg+a1] =? [reg+a2]
            return false;       // FIXME: evtl. vergleichen
        else if(t1 == 1) {      // [relo] =? [relo]
#define CMP(FIELD) if(l->reloc->FIELD < r->reloc->FIELD) return false; \
            if(l->reloc->FIELD > r->reloc->FIELD) return true;
            CMP(unitnum);
            CMP(rblock);
            CMP(rofs);
#undef CMP
            return false;
        } else
            return l->immediate > r->immediate;
    } else
        if(t1 == 2 || t2 == 2)  // Aliasing-Probleme!
            return false;
        return (t1 > t2);
}

/* true, wenn die Instruktionen von /p/ an auf Speicher zugreifen */
bool references_memory(CInstruction* p)
{
    while(p) {
        switch(p->insn) {
         case I_JMPN: case I_JMPF: case I_CALLN: case I_CALLF: case I_STRING:
            return true;
         default:
            for(int i = 0; i < 3; i++)
                if(p->args[i] && p->args[i]->type == CArgument::MEMORY)
                    return true;
        }
        p = p->next;
    }
    return false;
}

/*
 *  Verschiedene Dinge mit mov
 */
TAction check_mov(CInstruction* p)
{
    if(p->insn != I_MOV) return A_BAD;
    CInstruction* n = p->next;

    if(p->args[0]->type==CArgument::MEMORY && p->args[1]->type==CArgument::IMMEDIATE
       && p->args[1]->reloc==0) {
        /*
         *  mov mem,const
         */
        if(n && n->insn==I_MOV
           && n->args[0]->type==CArgument::MEMORY
           && n->args[1]->type==CArgument::IMMEDIATE &&
           n->args[1]->reloc==0) {
            if(p->opsize==1 && n->opsize==1) {
                /*
                 * mov(1) mem,const
                 * mov(1) mem,const
                 * -> mov(2) mem,const
                 */
                
                n->args[0]->inc_imm(1);
                if(*p->args[0]==*n->args[0]) {
                    /* erster Befehl lädt auf höhere Adresse */
                    n->args[0]->inc_imm(-1);
                    n->opsize = 2;
                    n->args[1]->immediate =
                        (n->args[1]->immediate & 255) +
                        p->args[1]->immediate * 256;
                    return A_DELETE;
                }
                n->args[0]->inc_imm(-2);
                if(*p->args[0]==*n->args[0]) {
                    /* zweiter Befehl lädt auf höhere Adresse */
                    //n->args[0]->inc_imm(1);
                    n->opsize = 2;
                    n->args[1]->immediate =
                        256 * n->args[1]->immediate + (p->args[1]->immediate & 255);
                    return A_DELETE;
                }
                n->args[0]->inc_imm(1);
            }
            /* zwei moves, in kanonische Reihenfolge bringen */
            if(do_sort_moves && arg_compare(p->args[0], n->args[0])) {
                CInstruction* x = (new CInstruction(I_MOV,
                                                    new CArgument(*p->args[0]),
                                                    new CArgument(*p->args[1])))
                    ->set_os(p->opsize);
                x->next = n->next;
                x->prev = n;
                n->next = x;
                if(x->next)
                    x->next->prev = x;
                return A_DELETE;
            }
        }

        if(do_size && p->opsize==2) {
            /*
             *  Word-Operation
             *  -> für const=0 und const=-1 ist ein AND/OR kürzer
             *     (wegen signed operanden), aber langsamer
             */
            if(p->args[1]->is_immed(0)) {
                p->insn = I_AND;
                return A_RESCAN;
            } else if(p->args[1]->is_immed(-1)) {
                p->insn = I_OR;
                return A_RESCAN;
            }
        }
    }

    if(p->args[0]->is_word_reg() && p->args[1]->is_word_reg()
       && (p->args[0]->reg==rAX || p->args[1]->reg==rAX)) {
        /*
         * mov reg,ax / mov ax,reg
         * -> durch `xchg' ersetzen (1 Byte kürzer), wenn das
         *    Quellregister nicht mehr benötigt wird.
         */
        TRegister src = p->args[1]->reg;
        CInstruction* n = p->next;
        bool doit = false;
        if(!n)
            doit=true;
        else if((n->insn==I_MOV || n->insn==I_LEA || n->insn==I_LES)
                && *n->args[0] == *p->args[1]
                && !n->args[1]->uses_reg(src))
            /* {MOV,LEA,LES} src,[FOO] */
            doit=true;
        else if(n->insn==I_IMUL && n->args[2]
                && *n->args[0] == *p->args[1]
                && !n->args[1]->uses_reg(src))
            /* IMUL src,[FOO],n */
            doit=true;
        
        if(doit) {
            p->insn = I_XCHG;
            return A_RESCAN;
        }
    }

    if(p->args[0]->is_byte_reg() && p->args[1]->is_immed(0) && p->next
       && p->next->insn==I_JCC
       && p->next->args[0]->type==CArgument::LABEL) {
        /*
         * mov rb,0 
         * jcc label
         */
        CInstruction* jump_insn = p->next;
        CInstruction* inc_insn = jump_insn->next;
        
        if(inc_insn && inc_insn->insn==I_INC && inc_insn->args[0]->is_word_reg()
           && jump_insn->args[0]->label==inc_insn->next) {
            /*
             * mov rb,0
             * jcc $+3
             * inc rw
             * -> setcc rb
             * auch bei !do_386, der Assembler drieselt das wieder auf
             */
            TRegister reg = p->args[0]->reg;
            if(reg>=rAL && reg<=rBL
               && reg_values[reg]==reg_values[inc_insn->args[0]->reg]) {
                p->insn = I_SETCC;
                p->param = jump_insn->param ^ 1;
                delete p->args[1];
                p->args[1] = 0;

                CInstruction* q = inc_insn->next;
                delete jump_insn;
                delete inc_insn;
                p->next = q;
                return A_RESCAN;
            }
        }
    }

    /*
     *   mov [foo], blurfl
     *   mov reg, [foo]
     *   -> mov reg, blurfl      | mov [foo],blurfl
     *      mov [foo], reg       | mov reg,blurfl
     */
    if(p->args[0]->type==CArgument::MEMORY             // Speicher-Argument
       && p->next                                      // gibt nächsten
       && p->next->insn==I_MOV                         // der ein mov ist
       && *p->args[0] == *p->next->args[1]             // memory operand ist derselbe
       && ((p->args[1]->type==CArgument::REGISTER      // mov [mem],reg
            && !p->args[1]->is_seg_reg())
           || (p->args[1]->type==CArgument::IMMEDIATE  // mov [mem],imm
               && !p->next->args[1]->is_seg_reg()))    // und kein mov sreg,[mem]
       )
    {
        if(p->args[0]->memory[0] == rBP && !references_memory(p->next->next)) {
            /* Zugriff auf Stackrahmen vor Ende der Funktion */
            CInstruction* n = p->next;
            delete n->args[1];
            n->args[1] = new CArgument(*p->args[1]);
            return A_DELETE;
        } else if(p->args[1]->type == CArgument::IMMEDIATE) {
            /* nutze erste Form */
            /* reg darf in mem nicht enthalten sein */
            /* zweite insn: Operanden vertauschen */
            if(p->args[0]->memory[0] != p->next->args[0]->reg
               && p->args[0]->memory[1] != p->next->args[0]->reg) {
                CArgument* a = p->next->args[0];               // `reg'
                p->next->args[0] = p->next->args[1];
                p->next->args[1] = a;
                /* erste insn ändern */
                delete p->args[0];
                p->args[0] = new CArgument(*a);
                return A_RESCAN;
            }
        } else {
            /* nutze zweite Form */
            CInstruction* n = p->next;
            delete n->args[1];
            n->args[1] = new CArgument(*p->args[1]);
            return A_RESCAN;
        }
    }

    /*
     *  mov a,b
     *  mov b,a
     */
    if(p->next->insn == I_MOV &&
       *p->args[0] == *p->next->args[1] &&
       *p->args[1] == *p->next->args[0]) {
        /* einen löschen */
        CInstruction* n = p->next;
        p->next = n->next;
        delete n;
        return A_RESCAN;
    }
    return A_BAD;
}

/*
 *  xchg-Befehle
 */
TAction check_xchg(CInstruction* p)
{
    if(p->insn!=I_XCHG || !p->next) return A_BAD;

    CInstruction* n = p->next;
    if(n->insn != I_XCHG && n->insn != I_MOV)
        return A_BAD;
    
    if((*p->args[0]==*n->args[0] && *p->args[1]==*n->args[1])
       || (*p->args[1]==*n->args[0] && *p->args[0]==*n->args[1])) {
        if(p->next->insn == I_XCHG) {
            /*
             *  xchg foo,bar
             *  xchg foo,bar
             *  -> löschen
             */
            p->next = n->next;
            delete n;
            return A_DELETE;
        } else {
            /*
             *  xchg foo,bar
             *  mov foo,bar
             *  -> xchg löschen, mov umdrehen
             */
            CArgument* a = n->args[0];
            n->args[0] = n->args[1];
            n->args[1] = a;
            return A_DELETE;
        }
    }
    return A_BAD;
}

/*
 *  mov reg,[foo]
 *  op reg,{reg,imm}
 *  mov [foo],reg
 */
TAction check_maybe_unary(CInstruction* p)
{
    /* mov reg,[foo] */
    if(p->insn != I_MOV || (!p->args[0]->is_word_reg() && !p->args[0]->is_byte_reg())
       || p->args[1]->type != CArgument::MEMORY)
        return A_BAD;

    /* op reg,{imm,mem} */
    CInstruction* op = p->next;
    if(op->insn != I_ADD && op->insn != I_OR && op->insn != I_ADC
       && op->insn != I_SBB && op->insn != I_AND && op->insn != I_SUB
       && op->insn != I_XOR)
        return A_BAD;
    if(!(*op->args[0] == *p->args[0]) ||
       (op->args[1]->type != CArgument::IMMEDIATE && op->args[1]->type != CArgument::REGISTER)
       || op->args[1]->uses_reg_part(p->args[0]->reg))
        return A_BAD;

    /* mov [foo],reg */
    CInstruction* mov = op->next;
    if(mov->insn != I_MOV || !(*mov->args[0] == *p->args[1])
       || !(*mov->args[1] == *p->args[0]))
        return A_BAD;

    /* can we modify the register? */
    bool canmod = can_modify_reg(mov->next, p->args[0]->reg);

    /* change operands in /op/ statement */
    delete op->args[0];
    op->args[0] = new CArgument(*p->args[1]);
    op->opsize |= p->opsize;

    if(canmod) {
        /* don't need to reload register */
        op->next = mov->next;
        delete mov;
    } else {
        /* need reload */
        CArgument* a = mov->args[0];
        mov->args[0] = mov->args[1];
        mov->args[1] = a;
    }

    return A_DELETE;
}

/*
 *  push [mem]
 *  mov reg,[mem]
 *  -> tauschen, dann wird push kürzer
 */
TAction check_push_reg(CInstruction* p)
{
    if(p->insn != I_PUSH || p->args[0]->type != CArgument::MEMORY
       || p->next->insn != I_MOV || p->next->opsize != 2)
        return A_BAD;

    CInstruction* n = p->next;
    if(!(*p->args[0] == *n->args[1]) || !n->args[0]->is_word_reg()
       || p->args[0]->uses_reg(n->args[0]->reg))
        return A_BAD;

    n->insn = I_PUSH;
    delete n->args[1];
    n->args[1] = 0;

    p->insn = I_MOV;
    p->args[1] = p->args[0];
    p->args[0] = new CArgument(n->args[0]->reg);

    return A_RESCAN;
}

/*
 *  shl foo,N
 *  shl foo,M
 *  => shl foo, N+M
 *  (passiert bei Arrayindizierung pword^[2*i])
 *
 *  shl reg,1
 *  => add reg,reg
 */
TAction check_shift(CInstruction* p)
{
    if(p->insn == I_SHL && p->args[1]->type == CArgument::IMMEDIATE
       && !p->args[1]->reloc) {
        if(p->next->insn == I_SHL      
           && p->next->args[1]->type == CArgument::IMMEDIATE
           && !p->next->args[1]->reloc
           && *p->args[0] == *p->next->args[0]) {
            p->next->args[1]->immediate += p->args[1]->immediate;
            return A_DELETE;
        }
        if(p->args[1]->is_immed(1) && p->args[0]->type == CArgument::REGISTER) {
            delete p->args[1];
            p->args[1] = new CArgument(*p->args[0]);
            p->insn = I_ADD;
            return A_RESCAN;
        }
    }
    return A_BAD;
}

/*
 *  FOR-Anfang:
 *    mov [i],n        ->    mov [i],n-1
 *    jmp skip             cont:
 *  cont:                    inc [i]
 *    inc [i]              skip:
 *  skip:
 */
TAction check_for_init(CInstruction* p)
{
    if(p->insn != I_MOV
       || p->args[1]->type != CArgument::IMMEDIATE
       || !p->next
       || p->next->insn != I_JMPN
       || p->next->args[0]->type != CArgument::LABEL)
        return A_BAD;

    CInstruction* q = p->next->next;
    while(q && q->insn==I_LABEL)
        q = q->next;
    if(!q
       || (q->insn != I_INC && q->insn != I_DEC)
       || !(*q->args[0] == *p->args[0])
       || q->opsize != p->opsize
       || !q->next
       || q->next->insn != I_LABEL
       || p->next->args[0]->label != q->next)
        return A_BAD;

    CInstruction* j = p->next;         // jmp
    p->next = j->next;
    delete j;
    if(q->insn==I_INC)
        p->args[1]->inc_imm(-1);
    else
        p->args[1]->inc_imm(1);
    return A_RESCAN;
}

/*
 *  Arithmetik mit Nulloperand
 */
TAction check_zero_arit(CInstruction* p)
{
    if(!p->args[1] || !p->args[1]->is_immed(0))
        return A_BAD;
    switch(p->insn) {
     case I_AND:
        /* and foo,0
           -> xor foo,foo  [wenn möglich] */
        if(p->args[0]->type != CArgument::REGISTER)
            return A_BAD;
        p->insn = I_XOR;
        delete p->args[1];
        p->args[1] = new CArgument(*p->args[0]);
        return A_RESCAN;
     case I_OR:
     case I_SUB:
        /* or foo,0
           -> or foo,foo  [wenn möglich] */
        if(p->args[0]->type != CArgument::REGISTER)
            return A_BAD;
        p->insn = I_OR;
        delete p->args[1];
        p->args[1] = new CArgument(*p->args[0]);
        return A_RESCAN;
     default:
        return A_BAD;
    }
}

/*
 *  cwd
 *  mov cx,wert
 *  xor bx,bx
 *  call <un=sys_unit_offset, rt=30, rb=28, ro=0>
 *  => mov cx,wert
 *     imul cx
 *
 *  cwd
 *  call <un=sys_unit_offset, rt=30, rb=50, ro=0>
 *  => imul ax
 */
TAction check_cwd_longmul(CInstruction* p)
{
    if(p->insn != I_CWD)
        return A_BAD;
    
    CInstruction* movcx = p->next;

    if(!movcx)
        return A_BAD;
    if(movcx->insn==I_CALLF) {
        if(movcx->args[0]->type != CArgument::IMMEDIATE)
            return A_BAD;
        CRelo* r = movcx->args[0]->reloc;
        if(!r || r->unitnum!=sys_unit_offset || r->rtype!=CODE_PTR_REF
           || r->rblock != SYS_LONG_SQR
           || r->rofs!=0)
            return A_BAD;
        delete movcx->args[0];
        movcx->args[0] = new CArgument(TRegister(rAX));
        movcx->insn = I_IMUL;
        movcx->opsize = 2;
        return A_DELETE;
    }
    
    if(!movcx || movcx->insn!=I_MOV || !movcx->args[0]->is_reg(rCX)
       || movcx->args[1]->type != CArgument::IMMEDIATE
       || movcx->args[1]->reloc || (movcx->args[1]->immediate & 0x8000))
        return A_BAD;

    CInstruction* xorbx = movcx->next;
    if(!xorbx || xorbx->insn != I_XOR || !xorbx->args[0]->is_reg(rBX)
       || !xorbx->args[1]->is_reg(rBX))
        return A_BAD;

    CInstruction* call = xorbx->next;
    if(!call || call->insn != I_CALLF || call->args[0]->type != CArgument::IMMEDIATE)
        return A_BAD;
    CRelo* r = call->args[0]->reloc;
    if(!r || r->unitnum!=sys_unit_offset || r->rtype!=CODE_PTR_REF || r->rblock != SYS_LONG_MUL
       || r->rofs != 0)
        return A_BAD;

    delete xorbx->args[0];
    delete xorbx->args[1];
    delete xorbx->args[2];
    xorbx->args[0] = new CArgument(rCX);
    xorbx->args[1] = xorbx->args[2] = 0;
    xorbx->insn = I_IMUL;
    xorbx->next = call->next;
    xorbx->next->prev = xorbx;
    delete call;
    return A_DELETE;
}

/*
 *   jcc SKIP
 *   mov T, A1         mov T, A1
 *   jmp FOO      =>   jncc FOO
 * SKIP:               mov T, A2
 *   mov T, A2
 *
 *   wenn T unabhängig von A2
 */
TAction check_double_mov(CInstruction* i)
{
    if(i->insn != I_JCC)
        return A_BAD;

    CInstruction* m1 = i->next;
    if(!m1 || m1->insn != I_MOV)
        return A_BAD;

    CInstruction* j = m1->next;
    if(!j || j->insn != I_JMPN)
        return A_BAD;

    CInstruction* l = j->next;
    if(l != i->args[0]->label)
        return A_BAD;

    CInstruction* m2 = l->next;
    if(!m2 || m2->insn != I_MOV)
        return A_BAD;

    if(*m2->args[0] != *m1->args[0])
        return A_BAD;

    if(m1->args[0]->type == CArgument::REGISTER
       && m2->args[1]->uses_reg(m1->args[0]->reg)) {
        return A_BAD;
    }

    if(m1->args[0]->type == CArgument::REGISTER || do_size) {
        j->insn = I_JCC;
        j->param = i->param ^ 1;
        return A_DELETE;
    } else {
        return A_BAD;
    }
}

/*
 *   mov REG, <anything>
 *   pop REG1
 *   -> tauschen, wenn unabhängig
 *
 *   push <anything>
 *   mov REG, <anything>
 *   -> tauschen, wenn unabhängig (Ziel: DFA möglich machen)
 */
TAction check_mov_pop(CInstruction* i)
{
    CInstruction* n;
    if(i->insn == I_MOV && i->next->insn == I_POP) {
        if(i->args[0]->type != CArgument::REGISTER 
           || i->next->args[0]->type != CArgument::REGISTER)
            return A_BAD;
        if(i->args[0]->reg == rSP)
            return A_BAD;
        if(i->args[1]->uses_reg(i->next->args[0]->reg))
            return A_BAD;
        n = new CInstruction(I_MOV,
                             new CArgument(*i->args[0]),
                             new CArgument(*i->args[1]));
    } else if(i->insn == I_PUSH && i->next->insn == I_MOV) {
        if(i->next->args[0]->type != CArgument::REGISTER)
            return A_BAD;
        TRegister reg = i->next->args[0]->reg;
        if(reg == rSP || i->args[0]->uses_reg(reg) || i->next->opsize != 2)
            return A_BAD;
        n = new CInstruction(I_PUSH, new CArgument(*i->args[0]));
    } else
        return A_BAD;

    n->opsize = i->opsize;
    n->next = i->next->next;
    n->prev = i->next;
    i->next->next = n;
    n->next->prev = n;

    return A_DELETE;
}

TAction check_push_pop(CInstruction* i)
{
    if(i->insn != I_PUSH || i->next->insn != I_POP
       /*|| !(*i->args[0] == *i->next->args[0])*/)
        return A_BAD;

    if(*i->args[0] == *i->next->args[0]) {
        CInstruction* n = i->next;
        i->next = n->next;
        i->next->prev = i;
        delete n;
    } else {
        /*
         *  push X
         *  pop Y
         *  => mov Y, X
         *  Der Einfachheit halber: Einer der Operanden muss ein
         *  Word-Register sein.
         */
        if(!i->args[0]->is_word_reg() && !i->next->args[0]->is_word_reg())
             return A_BAD;
        CInstruction* q = i->next;
        delete q->args[1];
        q->args[1] = new CArgument(*i->args[0]);
        q->set_os(2);
        q->insn = I_MOV;
    }
    return A_DELETE;
}

/*
 *  arit r1, irgendwas    => mov r2, r1
 *  mov  r2, r1              arit r2, irgendwas
 *
 *  arit r1, r1           => mov r2, r1
 *  mov  r2, r1              arit r2, r2
 *
 *  mov  r1, irgendwas    => mov r2, irgendwas
 *  mov  r2, r1
 *
 *  wenn zulässig (i.e., r1 wird nicht mehr benutzt)
 */
TAction check_reg_swap(CInstruction* i)
{
    if(!i->args[0] || i->args[0]->type != CArgument::REGISTER
       || !i->next || i->next->insn != I_MOV)
        return A_BAD;
    
    switch(i->insn) {
     case I_MOV:
        break;
     case I_ADD:
     case I_ADC:
     case I_SUB:
     case I_SBB:
     case I_OR:
     case I_XOR:
     case I_AND:
     case I_CMP:
        if(i->args[0]->is_seg_reg())
            return A_BAD;
        break;
     default:
        return A_BAD;
    }

    CInstruction* mov = i->next;
    if(*i->args[0] != *mov->args[1] || mov->args[0]->type != CArgument::REGISTER)
        return A_BAD;

    if (i->args[0]->is_seg_reg() || mov->args[0]->is_seg_reg())
        return A_BAD;

    TRegister r1 = i->args[0]->reg;
    TRegister r2 = mov->args[0]->reg;

    /* Form stimmt; Abhängigkeiten prüfen */
    if(i->args[1]->uses_reg(r2) || i->args[1]->uses_reg_part(r2))
        return A_BAD;

    CInstruction* p = mov->next;
    OperandSet in, out;
    while(1) {
        if(!p || is_break(p))
            return A_BAD;       // nicht beweisbar sicher
        compute_insn_dep(in, out, p);
        /* unsicher, wenn
           - in enthält r1 oder Teil oder Übermenge von r1
           sicher, wenn
           - out überschreibt r1 oder Übermenge von r1 */
        in.fix_regs();
        if(in.regs & (1 << r1))
            return A_BAD;       // beweisbar nicht sicher
        out.fix_regs_conservative();
        if(out.regs & (1 << r1))
            break;
        p = p->next;
    }

    /* ok --- durchführen */
    if(i->insn == I_MOV) {
        delete mov->args[1];
        mov->args[1] = new CArgument(*i->args[1]);
        return A_DELETE;
    } else {
        CArgument* arg2 =
            i->args[1]->is_reg(r1) ? new CArgument(r2)
                                   : new CArgument(*i->args[1]);
        CInstruction* a = new CInstruction(i->insn,
                                           new CArgument(*mov->args[0]),
                                           arg2);
        a->next = mov->next;
        mov->next = a;
        a->prev = mov;
        if(a->next)
            a->next->prev = a;
        return A_DELETE;
    }
}

/*
 *  push SREG
 *  push BX|SI|DI|BP
 *  push cs
 *  push <un=ofs_this_unit, rt=50, rb=block, ro=ofs>
 *  callf <un=SYSTEM, rt=30, rb=58, ro=0>
 *  => push SREG
 *     push ARG
 *     mov(2) [SREG:ARG], xx01h
 *  wenn ein Länge-1-String gepusht wird
 *     mov(2) [SREG:ARG], xx03h
 *     mov(2) [SREG:ARG+2], xxxxh
 *  bei Länge 3
 */
TAction check_push_char(CInstruction* i)
{
    if(i->insn != I_PUSH || !i->args[0]->is_seg_reg())
        return A_BAD;
    TRegister segment = i->args[0]->reg;

    CInstruction* push1 = i->next;
    if(push1->insn != I_PUSH || push1->args[0]->type != CArgument::REGISTER)
        return A_BAD;
    TRegister reg = push1->args[0]->reg;
    if(reg != rBX && reg != rSI && reg != rDI && reg != rBP)
        return A_BAD;

    CInstruction* push2 = push1->next;
    if(push2->insn != I_PUSH || !push2->args[0]->is_reg(rCS))
        return A_BAD;

    CInstruction* push3 = push2->next;
    if(push3->insn != I_PUSH || push3->args[0]->type != CArgument::IMMEDIATE)
        return A_BAD;
    CRelo* r = push3->args[0]->reloc;
    // FIXME? Annahme, daß Turbo nie solche Referenzen erzeugt, die
    // auf externe Blöcke zeigen
    if(!r || /*r->unitnum!=ref_this_unit ||*/ r->rtype != CODE_OFS_REF ||
       r->rblock != global_code_id)
        return A_BAD;

    CInstruction* call = push3->next;
    if(call->insn != I_CALLF || call->args[0]->type != CArgument::IMMEDIATE)
        return A_BAD;
    CRelo* rc = call->args[0]->reloc;
    if(!rc || rc->unitnum!=sys_unit_offset || rc->rtype!=CODE_PTR_REF || rc->rblock != SYS_SLOAD
       || rc->rofs != 0)
        return A_BAD;

    int rofs = r->rofs;
    int len = global_code_ptr[rofs];
    if(len != 1 && len != 3)
        return A_BAD;

    /* callf ersetzen */
    delete call->args[0];
    call->insn = I_MOV;
    call->opsize = 2;
    call->args[0] = new CArgument(reg, rNONE, 0, 0, segment);
    call->args[1] = new CArgument(256 * global_code_ptr[rofs+1] + len);

    /* push2, push3 eliminieren */
    push1->next = call;
    call->prev = push1;
    delete push2;
    delete push3;

    if(len == 3) {
        CInstruction* p = new CInstruction(I_MOV,
                new CArgument(reg, rNONE, 2, 0, segment),
                new CArgument(256 * global_code_ptr[rofs+3] + (unsigned char)global_code_ptr[rofs+2]));
        p->set_os(2);
        p->next = call->next;
        p->prev = call;
        call->next = p;
        p->next->prev = p;
    }

    return A_RESCAN;
}

/*
 *   les reg, [...]
 *   push es
 *   push di
 *   => push(4) [...]
 *      wenn zulaessig, nur im 386er Modus
 */
TAction check_push_ptr(CInstruction* i)
{
    if(!do_386 || i->insn != I_LES)
        return A_BAD;
    TRegister reg = i->args[0]->reg;

    CInstruction* pes = i->next;
    if(!pes || pes->insn != I_PUSH || !pes->args[0]->is_reg(rES))
        return A_BAD;

    CInstruction* pdi = pes->next;
    if(!pdi || pdi->insn != I_PUSH || !pdi->args[0]->is_reg(reg))
        return A_BAD;

    CInstruction* p = pdi->next;
    OperandSet in, out;
    int test = (1 << reg) | (1 << rES);
    while(1) {
        if(!p || is_break(p))
            return A_BAD;       // nicht beweisbar sicher
        compute_insn_dep(in, out, p);
        /* unsicher, wenn
           - in enthält r1 oder Teil oder Übermenge von r1
           sicher, wenn
           - out überschreibt r1 oder Übermenge von r1 */
        in.fix_regs();
        if(in.regs & test)
            return A_BAD;       // beweisbar nicht sicher
        out.fix_regs_conservative();
        if((out.regs & test) == test)
            break;
        p = p->next;
    }

    /* Ok */
    delete pdi->args[0];
    delete pes->args[0];
    pdi->args[0] = new CArgument(*i->args[1]);
    pes->args[0] = new CArgument(*i->args[1]);
    pes->args[0]->inc_imm(2);
    return A_DELETE;
}

/*
 *   mov [bp+],value        <- i
 *   jmp <label>            <- j
 *   ...
 *   label                  <- l
 *   mov reg, [mem]         <- m
 *   end-of-function
 *   => mov reg, value
 *      jmp label+1
 */
TAction check_func_return(CInstruction* i)
{
    if(i->insn != I_MOV || i->next->insn != I_JMPN)
        return A_BAD;
    if(i->args[0]->type != CArgument::MEMORY || i->args[0]->memory[0] != rBP)
        return A_BAD;
    CInstruction* j = i->next;
    if(j->args[0]->type != CArgument::LABEL)
        return A_BAD;

    CInstruction* l = j->args[0]->label;
//    if(l->ip < j->ip)
//        return A_BAD;

    CInstruction* m = l->next;
    if(m->insn != I_MOV || *m->args[1] != *i->args[0]
       || m->opsize != i->opsize
       || (i->args[1]->type == CArgument::IMMEDIATE && m->args[0]->is_seg_reg())
       || !m->next /* nicht noetig, vereinfacht aber die Implementation */)
        return A_BAD;

    if(references_memory(m->next))
        return A_BAD;
/*    for(CInstruction* p = m->next; p != 0; p = p->next) {
        switch(p->insn) {
         case I_JMPN: case I_JMPF: case I_CALLN: case I_CALLF:
            return A_BAD;
         default:
            for(int i = 0; i < 3; i++)
                if(p->args[i] && p->args[i]->type == CArgument::MEMORY)
                    return A_BAD;
        }
    }*/

    /* ok */
    CInstruction* lb;
    if(m->next->insn == I_LABEL)
        lb = m->next;
    else {
        lb = new CInstruction(I_LABEL);
        lb->next = m->next;
        m->next = lb;
        lb->ip = lb->next->ip;
    }

    lb->inc_ref();
    delete i->args[0];
    i->args[0] = new CArgument(*m->args[0]);
    delete j->args[0];
    j->args[0] = new CArgument(lb);
    return A_RESCAN;
}

/*
 *  Verschiedene Arithmetik-Sachen
 */
TAction check_int_arit(CInstruction* i)
{
    if(i->insn == I_AND && i->args[0]->is_byte_reg() 
       && i->args[1]->type == CArgument::IMMEDIATE && !i->args[1]->reloc
       && i->next->insn == I_XOR) {
        TRegister rl = i->args[0]->reg;
        TRegister rh = TRegister(rl + 4);
        if(rl >= rAL && rl <= rBL
           && i->next->args[0]->is_reg(rh)
           && i->next->args[1]->is_reg(rh)) {
            /*
             *  and RL, N
             *  xor RH, RH
             *  -> and RX, N
             */
            i->args[0]->reg = TRegister(rl - rAL + rAX);
            i->args[1]->immediate &= 0xFFU;
            CInstruction* n = i->next;
            i->next = n->next;
            delete n;
            return A_RESCAN;
        }
    }
    return A_BAD;
}

/*
 *  "tote" Vergleiche loeschen. Entstehen z.B. bei
 *  IF (longvar>=0) AND (longvar<100) ...
 */
TAction check_dead_tests(CInstruction* i)
{
    if (i->insn == I_CMP && flag_check(i->next))
        return A_DELETE;
    return A_BAD;
}

TAction last_function(CInstruction* i)
{
    return A_CONTINUE;
}

/* alle Optimierer-Funktionen */
TAction (*functions[])(CInstruction* i) = {
    check_nop,
    check_jump,
    check_cmp,
    check_zerotest,
    check_zerotest_jb,
    check_smalladd,
    check_mov,
    check_xchg,
    check_shift,
    check_push_reg,
    check_for_init,
    check_maybe_unary,
    check_zero_arit,
    check_cwd_longmul,
    check_double_mov,
    check_mov_pop,
    check_push_pop,
    check_reg_swap,
    check_push_char,
    check_push_ptr,
    check_func_return,
    check_int_arit,
    check_dead_tests,
    last_function
};

/*
 *  Hauptroutine der Peephole-Optimierung
 */
CInstruction* peephole_optimization(CInstruction* insn)
{
    CInstruction* prev = 0;
    CInstruction* p = insn;
    CInstruction* q;

    while(p) {
        int i = 0;
        while(1) {
            TAction a = functions[i](p);
            if(a==A_CONTINUE) {
                prev = p;
                p = p->next;
                break;
            } else if(a==A_RESCAN) {
                i = 0;
                changed = true;
            } else if(a==A_DELETE) {
                if(prev) prev->next = p->next;
                else insn = p->next;
                q = p;
                p = p->next;
                delete q;
                changed = true;
                break;
            } else
                i++;
        }
    }
    
    return insn;
}
