/*
 *  Peephole-Optimierung für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
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

typedef enum {
    A_BAD,         // Funktion paßt nicht auf insn
    A_CONTINUE,    // keine Änderung, trotzdem weitermachen
    A_DELETE,      // Befehl löschen
    A_RESCAN       // geändert, nochmal bewerten
} TAction;

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
    if(!p->next || p->next->insn != I_JMPN || p->next->args[0]->type != CArgument::LABEL)
        return A_BAD;
    
    if(p->args[0]->label == p->next->next) {
        /* inverse conditional jump */

        /* kann der Sprung umgedreht werden? */
        int distance = p->next->args[0]->label->ip - p->ip;
        if((distance > -123 && distance < 127) || do_386) {
            /* ja! */
            CInstruction* n = p->next;
            n->insn = I_JCC;
            n->param = p->param ^ 1;
            return A_DELETE;
        }
    } else if(*p->args[0] == *p->next->args[0]) {
        /* Zwei Sprünge - eine Meinung */
        return A_DELETE;
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
        || p->insn==I_AND || p->insn==I_XOR || p->insn==I_INC
        || p->insn==I_DEC)
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
    if(p->insn!=I_ADD || p->insn!=I_SUB || p->opsize!=2
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

    if(!flag_check(p->next)) return A_BAD; // Flags beachten
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
        if(p->opsize==1 && n && n->insn==I_MOV
           && n->opsize==1 && n->args[0]->type==CArgument::MEMORY
           && n->args[1]->type==CArgument::IMMEDIATE &&
           n->args[1]->reloc==0) {
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
        if(p->args[1]->type == CArgument::IMMEDIATE) {
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
       (op->args[1]->type != CArgument::IMMEDIATE && op->args[1]->type != CArgument::MEMORY)
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
 *  Mehrere shl's zu einem verbinden
 *  (passiert bei Arrayindizierung pword^[2*i])
 */
TAction check_shift(CInstruction* p)
{
    if(p->insn == I_SHL && p->next->insn == I_SHL
       && p->args[1]->type == CArgument::IMMEDIATE
       && p->next->args[1]->type == CArgument::IMMEDIATE
       && !p->args[1]->reloc && !p->next->args[1]->reloc
       && *p->args[0] == *p->next->args[0]) {
        p->next->args[1]->immediate += p->args[1]->immediate;
        return A_DELETE;
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
    last_function };

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
