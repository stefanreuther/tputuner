/*
 *  Optimierer für tputuner
 *
 *  (c) copyright 1998,1999,2000 by Stefan Reuther
 *
 *  Dieses Modul enthält die `einfachen' Optimierungen und die
 *  Steuerroutine, die alle Optimierungen aufruft.
 */
#include <fstream>
#include <string>
#include <cstdio>
#include "optimize.h"
#include "insn.h"
#include "disassemble.h"
#include "dfa.h"
#include "global.h"
#include "peephole.h"
#include "regalloc.h"
#include "cse.h"

using std::ofstream;
using std::flush;
using std::cout;
using std::endl;

bool changed;
char* global_code_ptr;
int global_code_id;
bool can_remove_stackframe;
int excluded_func_num;
TFuncName* excluded_funcs;
char* unit_name;

CInstruction* remove_instruction(CInstruction* insn, CInstruction** ainsn0, bool forward)
{
    if(insn->prev)
        insn->prev->next = insn->next;
    else if(ainsn0)
        *ainsn0 = insn->next;
    if(insn->next)
        insn->next->prev = insn->prev;
    CInstruction* i = forward?insn->next:insn->prev;
    delete insn;
    changed = true;
    return i;
}

CInstruction* insert_instruction(CInstruction* insn, CInstruction* insn1, CInstruction** ainsn0, bool after)
{
    if(!insn1) {
        insn1 = *ainsn0;
        after = false;
    }
    if(after) {
        if(insn1->next) {
            insn1->next->prev = insn;
            insn->next = insn1->next/*->next*/;
        } else
            insn->next = 0;
        insn1->next = insn;
        insn->prev = insn1;
    } else {
        if(insn1->prev) {
            insn1->prev->next = insn;
            insn->prev = insn1->prev/*->prev*/;
        } else {
            insn->prev = 0;
            if(ainsn0)
                *ainsn0 = insn;
        }
        insn1->prev = insn;
        insn->next = insn1;
    }
    changed = true;
    return insn;
}

/*
 *  Unbenutzten Code entfernen
 */
CInstruction* remove_unused_code(CInstruction* insn)
{
    bool used_bp = false;
    CInstruction* insn0 = insn;
    CInstruction* prev = 0;

    while(insn) {
        insn->prev = prev;

        if(do_remframe && can_remove_stackframe) {
            /* wird BP benutzt? */
            if(!used_bp) for(int i=0; i<3; i++) if(insn->args[i]) {
                if(insn->insn==I_CALLF || insn->insn==I_CALLN) {
                    used_bp = true;
                } else if(insn->args[i]->type==CArgument::REGISTER
                   && insn->args[i]->reg==rBP) {
                    if(insn->insn!=I_PUSH && insn->insn!=I_POP && insn->insn!=I_MOV)
                        used_bp=true;
                } else if(insn->args[i]->type==CArgument::MEMORY) {
                    if(insn->args[i]->memory[0]==rBP
                       || insn->args[i]->memory[1]==rBP)
                        used_bp=true;
                }
            }
        }

        if(insn->insn == I_JMPN || insn->insn == I_JMPF) {
            /*
             *    JMP <xxx>
             *    [xxx] <- unerreichbarer Code
             *    label`n'
             */
            CInstruction* ip = insn->next;
            int n = 0;
            while(ip) {
                CInstruction* p = ip;
                if(ip->insn == I_LABEL && ip->param!=0) break;
                ip = ip->next;
                delete p;
                n++;
            }
            if(n) changed=true;
            insn->next = ip;
        }
        prev = insn;
        insn = insn->next;
    }

    /*
     *  BP wird nicht benutzt -> Stackrahmen-Auf/Abbau entfernen
     */
    if(do_remframe && can_remove_stackframe && !used_bp && insn0 && prev) {
        // insn0 = erste insn
        // prev = letzte
        int entry_count = 0; // Anzahl Befehle für Eintrittscode
        int exit_count = 0;  // Für Austrittscode

        if(insn0->insn == I_ENTER)
            /* ENTER x,y */
            entry_count = 1;
        else if(insn0->insn == I_PUSH && insn0->args[0]->is_reg(rBP)) {
            insn = insn0->next;
            if(insn->insn == I_MOV && insn->args[0]->is_reg(rBP)
               && insn->args[1]->is_reg(rSP))
                /* PUSH BP / MOV BP,SP */
                entry_count = 2;
        }

        if((prev->insn==I_RETN || prev->insn==I_RETF) && entry_count) {
            prev = prev->prev;
            if(prev->insn == I_LEAVE)
                /* LEAVE */
                exit_count = 1;
            else if(prev->insn == I_POP && prev->args[0]->is_reg(rBP)) {
                insn = prev->prev;
                if(insn->insn == I_MOV && insn->args[0]->is_reg(rSP)
                   && insn->args[1]->is_reg(rBP))
                    /* MOV SP,BP / POP BP */
                    exit_count = 2;
            }
        }

        if(entry_count && exit_count) {
            /* ok, bekannter Entry/Exitcode gefunden -> entfernen */

            /* Eintrittscode */
            while(entry_count--) {
                remove_instruction(insn0, &insn0, true);
/* [OBSOLETE]
                insn = insn0;
                insn0 = insn0->next;
                insn0->prev = 0;
                insn0->next->prev = insn0;
                delete insn;
*/
            }
            /* Exitcode */
            while(exit_count--) {
                prev = remove_instruction(prev, &insn0, false);
/* [OBSOLETE]
                insn = prev;
                prev = prev->prev;
                if(prev)
                    prev->next = insn->next;
                else
                    insn0 = insn->next;
                delete insn;
*/
            }
            /* wir setzen ein Label an den Anfang */
            /* mindestens die DFA kann den 1. Befehl einer Folge nicht */
            /* löschen - und Labels werden von der nicht gelöscht */
            insn = new CInstruction(I_LABEL);
            insn->param = 5; /* dummy: 0 wäre löschbar, 1 wäre zurück tracebar */
            insert_instruction(insn, insn0, &insn0, false);
/* [OBSOLETE]
            insn->next = insn0;
            insn0 = insn;
            changed = true;
*/
        }
    }

    return insn0;
}

/*
 *  Optimierung redundanter Sprünge
 */
void reduce_redundant_jumps(CInstruction* insn)
{
    while(insn) {
        if((insn->insn == I_JMPN || insn->insn == I_JCC
              || insn->insn == I_JCXZ || insn->insn == I_LOOP)
           && insn->args[0]->type==CArgument::LABEL) {
            /* es ist ein Sprung an ein Label */
            while(1) {
                CInstruction* target = insn->args[0]->label;
                /* Ziel ist kein Sprung */
                if(target->next->insn != I_JMPN) break;

                /* Sprungkette führt nicht zu Label */
                if(target->next->args[0]->type != CArgument::LABEL)
                    break;
                int difference = target->next->args[0]->label->ip - insn->ip;

                /* Sprung außerhalb Reichweite */
                if(difference<-127 || difference>126) break;

                /* wir können optimieren */
                delete insn->args[0];
                insn->args[0] = new CArgument(*target->next->args[0]);
                changed=true;
            }
        }
        if(insn) insn = insn->next;
    }
}

/*
 *  Berechnet Offsets der Befehle neu
 */
CCWCounter* recalc_offsets(CInstruction* insn)
{
    int base_offset = insn->ip;
    CCWCounter* w = new CCWCounter();

    while(insn) {
        insn->ip = base_offset + w->bytes;
        try {
            insn = insn->assemble(*w);
        }
        catch(string& s) {
            if(insn->ip != base_offset + w->bytes) {
                /* ignore */
                /* diese Situation tritt auf, wenn ein vorwärtsgerichteter
                 * short-jump kurzzeitig zu lang wird, da er auf einem
                 * kleineren Offset steht, das Ziel aber noch nicht neu-
                 * berechnet wurde */
            } else
                throw string("Logic error: ") + s;
        }
        // [OBSOLETE] insn = insn->next;
    }

    return w;
}

/*
 *  Kontrolldump
 */
void write_file(int id, int pass, CInstruction* insn, CEntryBlock* entry)
{
    char filename[100];

#ifdef __MSDOS__
    sprintf(filename, "block%03x.p%d", id, pass);
#else
    sprintf(filename, "block%03x.pass%d", id, pass);
#endif

    ofstream f(filename);

    if(entry->name.length())
        f << "// function: " << entry->name << endl;
    while(insn) {
        insn->print(f);
        insn = insn->next;
    }
}

/*
 *  Reassembliert die Funktion
 */
void reassemble(CNewCode* nc, int my_offset, CInstruction* _insn, CCWCounter* wc)
{
    int reassembly_counter = 0;
    CInstruction* insn;
    while(1) {
     cont:
        cout << "r" << ++reassembly_counter << " " << flush;
        /* CodeWriter initialisieren */
        nc->code_size = wc->bytes;
        nc->extra_code_size = wc->extra_bytes;
        nc->code = new char[nc->code_size];
        nc->relo_size = 8*wc->relos;
        nc->relos = new char[nc->relo_size];
        CCWMemory* w = new CCWMemory(nc->code, nc->code_size,
                                     nc->relos, nc->relo_size,
                                     my_offset);

        /* assemblieren */
        insn = _insn;
        while(insn) {
            if(insn->ip != w->ip) {
                if(reassembly_counter < 10) {
                    insn->ip = w->ip;
                    delete wc;
                    delete w;
                    delete[] nc->code;
                    delete[] nc->relos;
                    wc = recalc_offsets(_insn);
                    goto cont;
                } else
                    throw string("Reassembly error: can't stabilize code, giving up");
            }
            insn = insn->assemble(*w);
            // [OBSOLETE] insn=insn->next;
        }

        if(w->code_left || w->relo_left) {
            throw string("Reassembly error: code/relo size predicted wrong");
        }
        delete w;
        delete wc;
        break;
    }
}

/*
 *  Sprünge verzögern, um gemeinsamen Code zu nutzen
 */
void late_jumps(CInstruction* insn)
{
    make_backlinks(insn);
    while(insn) {
        CInstruction* next = insn->next;
        if(insn->prev && insn->next && insn->insn==I_JCC &&
           insn->args[0]->type==CArgument::LABEL) {
            CInstruction* here = insn;
            CInstruction* there = insn->args[0]->label;
            while(here->next && there->next) {
                here = here->next;
                there = there->next;
                if(!(*here == *there)) break;
                /* continue, wenn der Befehl die Flags nicht ändert */
                switch(here->insn) {
                 case I_MOV:
                 case I_LES:
                 case I_LDS:
                 case I_XCHG:
                 case I_LEA:
                 case I_POP:
                 case I_POPA:
                 case I_PUSH:
                 case I_PUSHF:
                 case I_PUSHA:
                 case I_JCC:
                 case I_SETALC:
                 case I_XLAT:
                    continue;
                 case I_SETCC:
                    if(do_386) continue;
                    break;
                 case I_INC:
                 case I_DEC:
                    if(insn->param==2 || insn->param==3) continue;
                    break;
                 /* RET/JMP *nicht* überspringen */
                 default:
                    break;
                }
                break;
            }
            /* here, there -> erster nicht-übereinstimmender Befehl
             * insn -> Sprung */
            if(here && there && here!=insn->next && here!=insn) {
                /* ok, was gespart */
                CInstruction* label = there;
                if(there->insn != I_LABEL) {
                    /* label vor there einbauen */
                    label = new CInstruction(I_LABEL);
                    label->ip = there->ip;
                    insert_instruction(label, there, 0, false);
/* [OBSOLETE]
                    label->next = there;
                    label->prev = there->prev;
                    label->prev->next = label;
                    there->prev = label;
*/
                }
                /* Neuen Sprung einbauen */
                label->inc_ref();
                CInstruction* jump = new CInstruction(I_JCC, new CArgument(label));
                jump->param = insn->param;
                jump->ip = here->ip;
                insert_instruction(jump, here, 0, false);
/* [OBSOLETE]
                jump->next = here;
                jump->prev = here->prev;
                jump->prev->next = jump;
                here->prev = jump;
*/
                /* und alten Sprung weg */
                remove_instruction(insn, 0, true);
/* [OBSOLETE]
                insn->prev->next = insn->next;
                insn->next->prev = insn->prev;
                delete insn;
                changed = true;
*/
            }
        }
        insn = next;
    }
}

/*
 *  Sprünge vorziehen, um gemeinsamen Code zu nutzen
 *
 *       CALL foo
 *       JMP  skip           JMP  skip
 *       PUSH 2        ==>   PUSH 2
 *       CALL foo      skip: CALL foo
 * skip:
 */
void early_jumps(CInstruction* insn)
{
    make_backlinks(insn);

    while(insn) {
        CInstruction* next = insn->next;

        if(insn->insn==I_JMPN && insn->args[0]->type==CArgument::LABEL
           && insn->args[0]->label != insn->next) {
            /* ein Sprung, aber kein `JMP $+2' */
            CInstruction* here = insn->prev;
            CInstruction* there = insn->args[0]->label;

            while(here && there) {
                if(here->insn==I_LABEL) break;

                /* suche `echten' Befehl am Sprungziel */
                while(there && there->insn==I_LABEL)
                    there = there->prev;
                if (there) {
                    if(!(*there == *here)) break;

                    there = there->prev;
                    here = here->prev;
                }
            }

            /* wenn die insn sich nur im Quelloperanden unterscheiden:
             *   here --->   MOV   [foo],3
             *   there --->  MOV   [foo],AX
             * ändern in
             *   here --->   MOV   AX,3
             *               JMP
             *   there -->   LABEL
             *               MOV   [foo],AX */
            bool renamed = false;
            if(here && there && here->prev && there->prev
               && here->insn == there->insn &&
               (here->opsize == there->opsize || !here->opsize || !there->opsize)
               && here->args[0] && here->args[1]
               && there->args[0] && there->args[1]
               && *(here->args[0]) == *(there->args[0])
               && (here->insn != I_IMUL || here->args[2]==0)
               && (there->insn != I_IMUL || there->args[2]==0)
               && here->args[1]->type == CArgument::IMMEDIATE
               && (there->args[1]->is_word_reg() || there->args[1]->is_byte_reg())) {
                /* Übereinstimmung */
                delete here->args[0];
                here->args[0] = new CArgument(*there->args[1]);
                here->insn = I_MOV;
                there = there->prev;
                renamed = changed = true;
            }

            /* wir konnten was löschen */
            if(here && there && (renamed || (here != insn->prev))) {
                /* here ist der letzte zu erhaltende Befehl */
                /* wir springen hinter there */

                /* here --->   insn
                               JMP */
                /* there -->   insn
                               LABEL */

                CInstruction* label;
                if(there->next->insn != I_LABEL) {
                    if(there->insn == I_LABEL)
                        label = there;
                    else {
                        /* label erzeugen */
                        label = new CInstruction(I_LABEL);
                        label->ip = there->next->ip;
                        insert_instruction(label, there, 0, true);
/* [OBSOLETE]
                        label->next = there->next;
                        label->prev = there;
                        label->next->prev = label;
                        there->next = label;
*/
                    }
                } else {
                    label = there->next;
                }
                label->inc_ref();
                insert_instruction(new CInstruction(I_JMPN,
                                                    new CArgument(label)),
                                   here, 0, true);
/* [OBSOLETE]
                CInstruction* jump = new CInstruction(I_JMPN,
                                                      new CArgument(label));
                jump->next = here->next;
                jump->prev = here;
                jump->next->prev = jump;
                here->next = jump;
                changed = true;
*/
            }
        }

        insn = next;
    }
}

/* Translate between 286 (actually, 186) and 8086 instructions */
CInstruction* translate286(CInstruction* insn)
{
    make_backlinks(insn);
    CInstruction* insn0 = insn;
    while(insn) {
        CInstruction* next = insn->next;
        CInstruction* prev = insn->prev;
        /* (usually at the beginning of a function)
         * 286             8086
         * ENTER x,0 <---> PUSH BP
         *                 MOV  BP,SP
         *                 SUB  SP,x (only if x>0)
         *                 (shorter if x=y=0) */
        if(do_286
           && insn->insn==I_PUSH && insn->args[0]->is_reg(rBP)) {
            if (next && next->insn==I_MOV && next->args[0]->is_reg(rBP) && next->args[1]->is_reg(rSP)) {
                int entry_count = 2;
                CInstruction* i = next->next;
                int local_bytes = 0;
                if(i && i->insn==I_SUB && i->args[0]->is_reg(rSP) && i->args[1]->is_immed()) {
                    entry_count++;
                    local_bytes = i->args[1]->immediate;
                    i = i->next;
                }
                if(local_bytes) {
                    next = i;
                    insn = i->prev;
                    while(entry_count) {
                        insn = remove_instruction(insn, 0, false);
/* [OBSOLETE]
                        i = insn->prev;
                        delete insn;
                        insn = i;
*/
                        entry_count--;
                    }
                    insert_instruction(new CInstruction(I_ENTER,
                                                        new CArgument(local_bytes, 2),
                                                        new CArgument(0, 1)),
                                       next, &insn0, false);
/* [OBSOLETE]
                    insn = new CInstruction(I_ENTER,
                                             new CArgument(local_bytes, 2),
                                             new CArgument(0, 1));
                    if(prev)
                        prev->next = insn;
                    else
                        insn0 = insn;
                    insn->next = next;
                    next->prev = insn;
                    changed = true;
*/
                }
            }
        } else if(insn->insn==I_ENTER
                  && (!do_286 || (insn->args[0]->immediate==0 && insn->args[1]->immediate==0))) {
            if(insn->args[1]->immediate==0) {
                int local_bytes = insn->args[0]->immediate;
                remove_instruction(insn, &insn0, true);
/* [OBSOLETE]
                delete insn;
*/
                insn = insert_instruction(new CInstruction(I_PUSH,
                                                           new CArgument(rBP)),
                                          prev, &insn0, true);
/* [OBSOLETE]
                insn = new CInstruction(I_PUSH,
                                        new CArgument(rBP));
                insn->prev = prev;
                if(prev)
                    prev->next = insn;
                else
                    insn0 = insn;
*/
                insn = insert_instruction(new CInstruction(I_MOV,
                                                           new CArgument(rBP),
                                                           new CArgument(rSP)),
                                          insn, &insn0, true);
/* [OBSOLETE]
                insn->next = new CInstruction(I_MOV,
                                              new CArgument(rBP),
                                              new CArgument(rSP));
                insn->next->prev = insn;
                insn = insn->next;
*/
                if(local_bytes>0) {
                    insn = insert_instruction(new CInstruction(I_SUB,
                                                               new CArgument(rSP),
                                                               new CArgument(local_bytes, 2)),
                                              insn, &insn0, true);
/* [OBSOLETE]
                    insn->next = new CInstruction(I_SUB,
                                                  new CArgument(rSP),
                                                  new CArgument(local_bytes, 2));
                    insn->next->prev = insn;
                    insn = insn->next;
*/
                }
/* [OBSOLETE]
                insn->next = next;
                next->prev = insn;
                changed = true;
*/
            } else
                throw string("Can't translate ENTER x,y to 8086 code if y>0");
        /* (usually at the end of a function)
         * 286         8086
         * LEAVE <---> MOV SP, BP
         *             POP BP */
        } else if(do_286 && insn->insn==I_MOV && insn->args[0]->is_reg(rSP) && insn->args[1]->is_reg(rBP)) {
            if(next && next->insn==I_POP && next->args[0]->is_reg(rBP)) {
                insn = remove_instruction(insn, &insn0, true);
                remove_instruction(insn, &insn0, true);
/* [OBSOLETE]
                next = next->next;
                delete insn->next;
                delete insn;
*/
                insn = insert_instruction(new CInstruction(I_LEAVE), prev, &insn0, true);
/* [OBSOLETE]
                insn = new CInstruction(I_LEAVE);
                insn->prev = prev;
                if(prev)
                    prev->next = insn;
                else
                    insn0 = insn;
                insn->next = next;
                if(next)
                    next->prev = insn;
                changed = true;
*/
            }
        } else if(!do_286 && insn->insn==I_LEAVE) {
            remove_instruction(insn, &insn0, true);
/* [OBSOLETE]
            delete insn;
*/
            insn = insert_instruction(new CInstruction(I_MOV,
                                                       new CArgument(rSP),
                                                       new CArgument(rBP)),
                                      prev, &insn0, true);
/* [OBSOLETE]
            insn = new CInstruction(I_MOV,
                                    new CArgument(rSP),
                                    new CArgument(rBP));
*/
            insn = insert_instruction(new CInstruction(I_POP,
                                                       new CArgument(rBP)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            insn->next = new CInstruction(I_POP,
                                          new CArgument(rBP));
            insn->prev = prev;
            if(prev)
                prev->next = insn;
            else
                insn0 = insn;
            insn->next->next = next;
            insn->next->prev = insn;
            changed = true;
*/
        /* 286            8086
         * shift x,y ---> shift x,1
         *                [...]     (y times)
         *                shift x,1 */
        } else if(!do_286 && (insn->insn == I_SHL || insn->insn == I_SHR || insn->insn == I_ROL || insn->insn == I_ROR
                              || insn->insn == I_ROR || insn->insn == I_RCL || insn->insn == I_RCR || insn->insn == I_SAR)
                  && insn->args[1]->is_immed() && insn->args[1]->immediate>1) {
            int shift_count = insn->args[1]->immediate;
            insn->args[1]->immediate = 1;
            insn->args[1]->imm_size = 1;
            while(--shift_count) {
                insn = insert_instruction(new CInstruction(insn->insn,
                                                           new CArgument(*insn->args[0]),
                                                           new CArgument(1, 1)),
                                          insn, &insn0, true);
/* [OBSOLETE]
                prev = insn;
                insn = new CInstruction(prev->insn,
                                        new CArgument(*prev->args[0]),
                                        new CArgument(1, 1));
                insn->prev = prev;
                prev->next = insn;
*/
            }
/* [OBSOLETE]
            insn->next = next;
            changed = true;
*/
        /* 286         8086
         * PUSHA <---> PUSH AX
         *             PUSH CX
         *             PUSH DX
         *             PUSH BX
         *             PUSH SP
         *             PUSH BP
         *             PUSH SI
         *             PUSH DI */
        } else if(!do_286 && insn->insn == I_PUSHA) {
            remove_instruction(insn, &insn0, true);
/* [OBSOLETE]
            delete insn;
*/
            insn = insert_instruction(new CInstruction(I_PUSH,
                                                       new CArgument(rAX)),
                                      prev, &insn0, true);
/* [OBSOLETE]
            insn = new CInstruction(I_PUSH,
                                    new CArgument(rAX));
            insn->prev = prev;
            if(prev)
                prev->next = insn;
            else
                insn0 = insn;
*/
            insn = insert_instruction(new CInstruction(I_PUSH,
                                                       new CArgument(rCX)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            CInstruction* i = new CInstruction(I_PUSH,
                                               new CArgument(rCX));
            insn->next = i;
            i->prev = insn;
*/
            insn = insert_instruction(new CInstruction(I_PUSH,
                                                       new CArgument(rDX)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            insn = new CInstruction(I_PUSH,
                                    new CArgument(rDX));
            i->next = insn;
            insn->prev = i;
*/
            insn = insert_instruction(new CInstruction(I_PUSH,
                                                       new CArgument(rBX)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            i = new CInstruction(I_PUSH,
                                 new CArgument(rBX));
            insn->next = i;
            i->prev = insn;
*/
            insn = insert_instruction(new CInstruction(I_PUSH,
                                                       new CArgument(rSP)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            insn = new CInstruction(I_PUSH,
                                    new CArgument(rSP));
            i->next = insn;
            insn->prev = i;
*/
            insn = insert_instruction(new CInstruction(I_PUSH,
                                                       new CArgument(rBP)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            i = new CInstruction(I_PUSH,
                                 new CArgument(rBP));
            insn->next = i;
            i->prev = insn;
*/
            insn = insert_instruction(new CInstruction(I_PUSH,
                                                       new CArgument(rSI)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            insn = new CInstruction(I_PUSH,
                                    new CArgument(rSI));
            i->next = insn;
            insn->prev = i;
*/
            insn = insert_instruction(new CInstruction(I_PUSH,
                                                       new CArgument(rDI)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            i = new CInstruction(I_PUSH,
                                 new CArgument(rDI));
            insn->next = i;
            i->prev = insn;
            i->next = next;
            next->prev = i;
            changed = true;
*/
        /* 286        8086
         * POPA <---> POP DI
         *            POP SI
         *            POP BP
         *            ADD SP,2
         *            POP BX
         *            POP DX
         *            POP CX
         *            POP AX */
        } else if(!do_286 && insn->insn == I_POPA) {
            remove_instruction(insn, &insn0, true);
/* [OBSOLETE]
            delete insn;
*/
            insn = insert_instruction(new CInstruction(I_POP,
                                                       new CArgument(rDI)),
                                      prev, &insn0, true);
/* [OBSOLETE]
            insn = new CInstruction(I_POP,
                                    new CArgument(rDI));
            insn->prev = prev;
            if(prev)
                prev->next = insn;
            else
                insn0 = insn;
*/
            insn = insert_instruction(new CInstruction(I_POP,
                                                       new CArgument(rSI)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            CInstruction* i = new CInstruction(I_POP,
                                               new CArgument(rSI));
            insn->next = i;
            i->prev = insn;
*/
            insn = insert_instruction(new CInstruction(I_POP,
                                                       new CArgument(rBP)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            insn = new CInstruction(I_POP,
                                    new CArgument(rBP));
            i->next = insn;
            insn->prev = i;
*/
            insn = insert_instruction(new CInstruction(I_POP,
                                                       new CArgument(rSP),
                                                       new CArgument(2, 2)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            i = new CInstruction(I_ADD,
                                 new CArgument(rSP),
                                 new CArgument(2, 2));
            insn->next = i;
            i->prev = insn;
*/
            insn = insert_instruction(new CInstruction(I_POP,
                                                       new CArgument(rBX)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            insn = new CInstruction(I_POP,
                                    new CArgument(rBX));
            i->next = insn;
            insn->prev = i;
*/
            insn = insert_instruction(new CInstruction(I_POP,
                                                       new CArgument(rDX)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            i = new CInstruction(I_POP,
                                 new CArgument(rDX));
            insn->next = i;
            i->prev = insn;
*/
            insn = insert_instruction(new CInstruction(I_POP,
                                                       new CArgument(rCX)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            insn = new CInstruction(I_POP,
                                    new CArgument(rCX));
            i->next = insn;
            insn->prev = i;
*/
            insn = insert_instruction(new CInstruction(I_POP,
                                                       new CArgument(rAX)),
                                      insn, &insn0, true);
/* [OBSOLETE]
            i = new CInstruction(I_POP,
                                 new CArgument(rAX));
            insn->next = i;
            i->prev = insn;
            i->next = next;
            next->prev = i;
            changed = true;
*/
        }
        insn = next;
    }
    return insn0;
}

/*
 *  Hauptprozedur der Optimierers
 *  id = Codeblock-Nummer
 *  code = zeigt auf Codestück
 *  code_size = Größe desselben
 *  my_offset = IP am Eintritt in den Code
 *  relo = Zeiger auf Relokationstabelle
 *  relo_count = Einträge (a 8 Byte) in der Tabelle
 *  entry = for function name, if available
 */
CNewCode* do_optimize(int id,
                      char* code, int code_size, int my_offset,
                      char* relo, int relo_count, CEntryBlock* entry)
{
    global_code_ptr = code;
    global_code_id  = id;
    code += my_offset;
    CInstruction* insn = disassemble(code, code_size, my_offset, relo, relo_count);
    int pass = 0;
    int exit_counter = 2;
    CCWCounter* w = 0;

    int status = 0;
    do {
        delete w;
        changed = false;
        if(do_dumps)
            write_file(id, pass, insn, entry);
        pass++;
        insn = translate286(insn);
        if(do_the_cse)
            do_cse(insn);
        if(do_early_jmp)
            early_jumps(insn);
        if(do_late_jmp)
            late_jumps(insn);
        if(do_jumpchains)
            reduce_redundant_jumps(insn);
        if(do_remunused)
            insn = remove_unused_code(insn);
        if(do_dfa && status==0)
            data_flow_analysis(insn);
        if(do_reg_alloc && status==1)
            register_allocation(insn);
        if(do_peephole)
            insn = peephole_optimization(insn);
        insn->ip = my_offset;
        w = recalc_offsets(insn);
        cout << pass << (changed?"! ":" ") << flush;
        if(changed) exit_counter=2;
        else exit_counter--;
        if(exit_counter <= 0) {
            status++;
            if(status < 2)
                exit_counter = 1;
        }
    } while(exit_counter > 0);
    code_size += w->extra_bytes;
    if(code_size < w->bytes) {
        cout << "Warning: new code got bigger - code block unchanged" << endl;
        return 0;
    }

    CNewCode* nc = new CNewCode;
    reassemble(nc, my_offset, insn, w);

    if (code_size!=nc->code_size || w->extra_bytes) {
        cout << " --> ";
        if (code_size!=nc->code_size) {
            cout << code_size - nc->code_size << " bytes saved";
            if (w->extra_bytes)
                cout << ", ";
        }
        if (w->extra_bytes)
            cout << w->extra_bytes << " bytes added";
        cout << " (new size = " << nc->code_size << ")";
    }
    cout << endl;

    return nc;
}
