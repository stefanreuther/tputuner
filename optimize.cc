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

bool changed;
char* global_code_ptr;
int global_code_id;

#define REMOVE_FRAME 0

/*
 *  Unbenutzten Code entfernen
 */
CInstruction* remove_unused_code(CInstruction* insn)
{
//    bool used_bp = false;
    CInstruction* insn0 = insn;
    CInstruction* prev = 0;

    while(insn) {
        insn->prev = prev;

#if REMOVE_FRAME
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
#endif
        
	if(insn->insn == I_JMPN || insn->insn == I_JMPF) {
	    /*
	     *    jmp <xxx>
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

#if REMOVE_FRAME
    /*
     *  BP wird nicht benutzt -> Stackrahmen-Auf/Abbau entfernen
     */
    if(!used_bp && insn0 && prev) {
        // insn0 = erste insn
        // prev = letzte
        int entry_count = 0; // Anzahl Befehle für Eintrittscode
        int exit_count = 0;  // Für Austrittscode

        if(insn0->insn == I_ENTER)
            /* enter X,Y */
            entry_count = 1;
        else if(insn0->insn == I_PUSH && insn0->args[0]->is_reg(rBP)) {
            insn = insn0->next;
            if(insn->insn == I_MOV && insn->args[0]->is_reg(rBP)
               && insn->args[1]->is_reg(rSP))
                /* push bp / mov bp,sp */
                entry_count = 2;
        }

        if((prev->insn==I_RETN || prev->insn==I_RETF) && entry_count) {
            prev = prev->prev;
            if(prev->insn == I_LEAVE)
                /* leave */
                exit_count = 1;
            else if(prev->insn == I_POP && prev->args[0]->is_reg(rBP)) {
                insn = prev->prev;
                if(insn->insn == I_MOV && insn->args[0]->is_reg(rSP)
                   && insn->args[0]->is_reg(rBP))
                    /* mov sp,bp / pop bp */
                    exit_count = 2;
            }
        }

        if(entry_count && exit_count) {
            /* ok, bekannter Entry/Exitcode gefunden -> entfernen */
            
            /* Eintrittscode */
            while(entry_count--) {
                insn = insn0;
                insn0 = insn0->next;
                delete insn;
            }
            /* Exitcode */
            while(exit_count--) {
                insn = prev;
                prev = prev->prev;
                prev->next = insn->next;
                delete insn;
            }
            /* wir setzen ein Label an den Anfang */
            /* mindestens die DFA kann den 1. Befehl einer Folge nicht */
            /* löschen - und Labels werden von der nicht gelöscht */
            insn = new CInstruction(I_LABEL);
            insn->param = 5; /* dummy: 0 wäre löschbar, 1 wäre zurück tracebar */
            insn->next = insn0;
            insn0 = insn;
            changed = true;
        }
    }
#endif

    return insn0;
}

/*
 *  Optimierung redundanter Sprünge
 */
void reduce_redundant_jumps(CInstruction* insn)
{
    while(insn) {
	if((insn->insn == I_JMPN || insn->insn == I_JCC || insn->insn == I_JCXZ)
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
	//insn = insn->next;
    }

    return w;
}

/*
 *  Kontrolldump
 */
void write_file(int id, int pass, CInstruction* insn)
{
    char filename[100];
    
#ifdef __MSDOS__
    sprintf(filename, "block%x.p%d", id, pass);
#else
    sprintf(filename, "block%x.pass%d", id, pass);
#endif
            
    ofstream f(filename);

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
	    //insn=insn->next;
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
                 case I_PUSH:
                 case I_JCC:
                    continue;
                 case I_SETCC:
                    if(do_386) continue;
                    break;
                 case I_INC:
                 case I_DEC:
                    if(insn->param==2 || insn->param==3) continue;
                    break;
                 /* ret/jmp *nicht* überspringen */
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
                    label->next = there;
                    label->prev = there->prev;
                    label->prev->next = label;
                    there->prev = label;
                }
                /* Neuen Sprung einbauen */
                label->inc_ref();
                CInstruction* jump = new CInstruction(I_JCC, new CArgument(label));
                jump->param = insn->param;
                jump->ip = here->ip;
                jump->next = here;
                jump->prev = here->prev;
                jump->prev->next = jump;
                here->prev = jump;
                /* und alten Sprung weg */
                insn->prev->next = insn->next;
                insn->next->prev = insn->prev;
                delete insn;
                changed = true;
            }
        }
        insn = next;
    }
}

/*
 *  Sprünge vorziehen, um gemeinsamen Code zu nutzen
 *
 *       call foo
 *       jmp  skip           jmp  skip
 *       push 2        ==>   push 2
 *       call foo      skip: call foo
 * skip:
 */
void early_jumps(CInstruction* insn)
{
    make_backlinks(insn);

    while(insn) {
        CInstruction* next = insn->next;
        
        if(insn->insn==I_JMPN && insn->args[0]->type==CArgument::LABEL
           && insn->args[0]->label != insn->next) {
            /* ein Sprung, aber kein `jmp $+2' */
            CInstruction* here = insn->prev;
            CInstruction* there = insn->args[0]->label;

            while(here && there) {
                if(here->insn==I_LABEL) break;
                
                /* suche `echten' Befehl am Sprungziel */
                while(there && there->insn==I_LABEL)
                    there = there->prev;
                if(!(*there == *here)) break;

                there = there->prev;
                here = here->prev;
            }

            /* wenn die insn sich nur im Quelloperanden unterscheiden:
             *   here --->   mov   [foo],3
             *   there --->  mov   [foo],ax
             * ändern in
             *   here --->   mov   ax,3
             *               JMP
             *   there -->   LABEL
             *               mov   [foo],ax */
            bool renamed = false;
            if(here && there && here->prev && there->prev
               && here->insn == there->insn &&
               (here->opsize == there->opsize || !here->opsize || !there->opsize)
               && here->args[0] && here->args[1]
               && there->args[0] && there->args[1]
               && *(here->args[0]) == *(there->args[0])
               && (here->insn != I_IMUL || here->args[3]==0)
               && (there->insn != I_IMUL || there->args[3]==0)
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
                        label->next = there->next;
                        label->prev = there;
                        label->next->prev = label;
                        there->next = label;
                    }
                } else {
                    label = there->next;
                }
                label->inc_ref();
                CInstruction* jump = new CInstruction(I_JMPN,
                                                      new CArgument(label));
                jump->next = here->next;
                jump->prev = here;
                jump->next->prev = jump;
                here->next = jump;
                changed = true;
            }
        }
        
        insn = next;
    }
}

/*
 *  Hauptprozedur der Optimierers
 *  id = Codeblock-Nummer
 *  code = zeigt auf Codestück
 *  code_size = Größe desselben
 *  my_offset = IP am Eintritt in den Code
 *  relo = Zeiger auf Relokationstabelle
 *  relo_count = Einträge (a 8 Byte) in der Tabelle
 */
CNewCode* do_optimize(int id,
		      char* code, int code_size, int my_offset,
		      char* relo, int relo_count)
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
	    write_file(id, pass, insn);
	pass++;
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
    if(code_size < w->bytes) {
	cout << "Warning: new code got bigger - code block unchanged" << endl;
	return 0;
    }
    
    CNewCode* nc = new CNewCode;
    reassemble(nc, my_offset, insn, w);
    
    cout << " --> " << code_size - nc->code_size << " bytes saved (new size = "
	 << nc->code_size << ")" << endl;

    return nc;
}
