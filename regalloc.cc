/*
 *  Registerallokierung für tputuner
 *
 *  (c) copyright 1999,2000 by Stefan Reuther
 */
#include "regalloc.h"
#include "optimize.h"

#define GLOBAL_ALLOC
/* für globale Register Allocation */

struct TEstimate {
    TEstimate* next;
    CArgument* arg;
    bool may_value;             // true, wenn Verwaltung als Wert erlaubt
    bool may_adr;
    bool used;
    int save_if_value;          // Ersparnisse, wenn als Wert verwaltet
    int save_if_adr;            // Ersparnisse, wenn als Adresse
    TEstimate(CArgument* a);
};

int changeable_args[] = {
    0,
    0,
    2, 2, 2, 2, 2,              // Transfer
    1, 1,                       // push/pop
    1, 1,                       // inc/dec
    2, 2, 2, 2, 2, 2, 2, 2,     // Arithmetik
    2, 1, 1, 1,                 // mul/div
    1, 1,                       // not/neg
    0, 0, 0, 0, 0, 0, 0,        // Programmtransfer
    0,
    0, 0,                       // cbw/cwd
    1, 1, 1, 1, 1, 1, 1,        // Shifts
    0, 0,                       // leave/enter
    1                           // setcc
};


/*
 *  Größe des Speicheroperanden /a/ (modrm, Overrides, disp)
 */
int mem_op_size(CArgument* a)
{
    int size = 1;               // modrm
    switch(a->segment) {        // override
     case rSS:
        if(!a->uses_reg(rBP))
            size++;
        break;
     case rDS:
        if(a->uses_reg(rBP))
            size++;
        break;
     default:
        size++;
    }
    if(a->reloc || a->immediate < -128 || a->immediate > 127)
        size += 2;
    else if(a->immediate != 0 || a->uses_reg(rBP))
        size ++;
    return size;
}

TEstimate::TEstimate(CArgument* a)
    : next(0), arg(a), may_value(true), may_adr(false), used(false),
      save_if_value(0), save_if_adr(0)
{
    switch(a->type) {
     case CArgument::MEMORY:
        /* bei konstanten Adressen: 3 Bytes für Adresse */
        if(a->memory[0] == rNONE && a->memory[1] == rNONE &&
           (a->segment == rNONE || a->segment == rDS)) {
            save_if_adr = -3;
            may_adr = true;
        }

        /* für Wert: benötigt `mov reg,[adr]' */
        save_if_value = -1 - mem_op_size(a);
        break;
     case CArgument::IMMEDIATE:
        /* 3 Bytes, um Immediate in Register zu laden */
        save_if_value = -3;
        break;
     default:
        cerr << "should not happen: neither memory nor immediate" << endl;
        a->print(cerr);
        cerr << endl;
    }
}

TEstimate* estimated_savings = 0;

/*
 *  Abschätzung für Argument /a/ erzeugen
 */
TEstimate* get_estimation(CArgument* a)
{
    TEstimate* p = estimated_savings;
    while(p) {
        if(*p->arg == *a)
            return p;
        p = p->next;
    }

    p = new TEstimate(a);
    p->next = estimated_savings;
    estimated_savings = p;
    return p;
}

/*
 *  Lesezugriff auf Wert /p/ bewerten (Abschätzen der Ersparnisse)
 *  /sex/ = true, wenn der Operand als Wort oder sign-extended byte
 *  verwaltet wird
 *
 *  have_modrm: ein modrm ist schon vorhanden
 */
TEstimate* estimate_read(CArgument* p, bool sex, bool have_modrm, bool znd)
{
    TEstimate* e;
    switch(p->type) {
     case CArgument::REGISTER:        /* keine Ersparnisse */
     case CArgument::LABEL:           /* should not happen */
        return 0;
     case CArgument::MEMORY:
        /* Speicheroperand */
        if(p->segment != rDS && p->segment != rSS)
            return 0;
        
        e = get_estimation(p);
        e->save_if_adr += mem_op_size(p) - 1;
        if(p->segment != rDS)
            e->save_if_adr--;
        if(!have_modrm)
            e->save_if_adr--;
        sex = true;
        /* FALLTHROUGH */
     case CArgument::IMMEDIATE:
        /* ein bis zwei Bytes */
        e = get_estimation(p);
        if(sex && p->reloc==0 && p->immediate>=-128 && p->immediate<=127)
            e->save_if_value--;
        if(!have_modrm)
            e->save_if_value--;
        if(znd)
            e->save_if_value--;
        e->save_if_value += 2;
        return e;
    }
    return 0;
}

/*
 *  Schreibzugriff auf Wert /p/ bewerten (Abschätzen der Ersparnisse)
 */
void estimate_write(CArgument* p, bool znd)
{
    TEstimate* e = estimate_read(p, false, true, znd);
    
    if(e)
        e->may_value = false;
}

/*
 *  Abschätzungs-Tabelle leeren
 */
void clear_estimates()
{
    while(estimated_savings) {
        TEstimate* p = estimated_savings;
        estimated_savings = estimated_savings->next;
        delete p;
    }
}

/*
 *  Abschätzen der Ersparnisse für Befehl /insn/
 *  znd = true, wenn dieser Befehl mit dem vorigen Befehl zusammen
 *  als 32bit-Operation codiert ist (Ersparnis ist dann geringer)
 */
void estimate_insn(CInstruction* insn, bool znd)
{
    switch(insn->insn) {
     case I_DEC: case I_INC: case I_NOT: case I_NEG: case I_POP:
     case I_SETCC:
     case I_ROR: case I_ROL: case I_RCL: case I_RCR:
     case I_SHL: case I_SHR: case I_SAR:
        /* Befehle mit einem Zieloperanden */
        estimate_write(insn->args[0], znd);
        break;
     case I_PUSH:
     case I_IDIV: case I_MUL: case I_DIV:
        /* Befehle mit einem Quelloperanden */
        estimate_read(insn->args[0], true, true, znd);
        break;
     case I_ADD: case I_OR: case I_ADC: case I_SBB:
     case I_AND: case I_SUB: case I_XOR:
        /* Ein Quell, ein Zieloperand */
        estimate_read(insn->args[1], !insn->args[0]->is_reg(rAX), !insn->args[0]->is_reg(rAX), znd);
        estimate_write(insn->args[0], znd);
        break;
     case I_CMP:
        /* Zwei Quelloperanden */
        estimate_read(insn->args[1], !insn->args[0]->is_reg(rAX), !insn->args[0]->is_reg(rAX), znd);
        estimate_read(insn->args[0], false, true, znd);
        break;
     case I_IMUL:
        /* Mischform: Ziel/Quell, oder Quell */
        if(insn->args[1]) {
            estimate_read(insn->args[1], false, true, znd);
            estimate_write(insn->args[0], znd);
        } else
            estimate_read(insn->args[0], false, true, znd);
        break;
     case I_MOV:
        /* Ziel und Quell, aber Quelle kann nicht kleiner werden */
         {
             bool is_A0 = insn->args[0]->is_reg(rAX) && insn->args[1]->type==CArgument::MEMORY
                 && insn->args[1]->memory[0]==rNONE && insn->args[1]->memory[1]==rNONE;
             bool is_mov_imm = insn->args[0]->type == CArgument::REGISTER
                 && insn->args[1]->type == CArgument::IMMEDIATE;
             estimate_read(insn->args[1], false, !(is_A0 || is_mov_imm), znd);
             estimate_write(insn->args[0], znd);
         }
         break;
     case I_XCHG:
        /* Zwei Zieloperanden */
        estimate_write(insn->args[0], znd);
        estimate_write(insn->args[1], znd);
        break;
     case I_LEA:
     case I_LES:
     case I_LDS:
        /* der 2. Operand kann nicht by-value verwendet werden */
        estimate_write(insn->args[1], znd);
        break;
     default:;
    }
}

/*
 *  Sucht besten Operanden, der in ein Register gepackt werden kann.
 *  may_adr = true, wenn auch Adressen möglich sind. adr = true,
 *  wenn ein Adressoperand gefunden wurde, sonst false (Wertoperand).
 */
TEstimate* find_best(bool& adr, bool may_adr)
{
    TEstimate* p = estimated_savings;
    TEstimate* best = 0;
    int best_save = 0;
    while(p) {
        if(!p->used) {
            if(p->arg->type == CArgument::MEMORY &&
               (p->arg->memory[0]!=rNONE || p->arg->memory[1]!=rNONE))
                p->may_value = p->may_adr = false;
            
            if(p->may_value && p->save_if_value > best_save) {
                best = p;
                best_save = p->save_if_value;
                adr = false;
            }
            if(may_adr && p->may_adr && p->save_if_adr > best_save) {
                best = p;
                best_save = p->save_if_adr;
                adr = true;
            }
        }
        p = p->next;
    }
    return best;
}

void replace_argument(CArgument*& arg, TRegister reg, bool adr)
{
    if(adr) {
        if(arg->reloc)
            delete arg->reloc;
        arg->reloc = 0;
        arg->immediate = 0;
        arg->memory[0] = reg;
        arg->memory[1] = rNONE;
    } else {
        delete arg;
        arg = new CArgument(reg);
    }
    changed = true;
}

/*
 *  Register-Allokation in dem durch /start/ und /end/ begrenzten
 *  basic block, zur Verfügung stehen Register, die in /regs_used/
 *  /false/ stehen haben.
 */
void optimize_basic_block(CInstruction* start, CInstruction* end, bool* regs_used)
{
    CArgument* replace[rMAX];
    CArgument* compare[rMAX];
    bool use_adr[rMAX];
    for(int i = 0; i < rMAX; i++) {
        compare[i] = 0;
        replace[i] = 0;
        use_adr[i] = false;
    }

    bool found = false;
    for(int i = rAX; i <= rDI; i++)
        if(!regs_used[i]) {
            bool v = false;
            TEstimate* p = find_best(v, (i==rBX || i==rSI || i==rDI));
            if(p) {
                p->used = true;
                replace[i] = new CArgument(*p->arg);
                compare[i] = new CArgument(*p->arg);
                if(v)
                    replace[i]->type = CArgument::IMMEDIATE;
                use_adr[i] = v;
                /* Ändern der Argumente zerstört p->args */
                found = true;

                for(TEstimate* q = estimated_savings; q != 0; q = q->next) {
                    int i = p->arg->adr_diff(q->arg);
                    if(i && i > -127 && i < 127)
                        q->used = true;
                }
            }
        }
    clear_estimates();
    if(!found)
        return;

    /* Änderungsbefehle erzeugen */
    for(int i = rAX; i <= rDI; i++)
        if(replace[i]) {
            CInstruction* n = new CInstruction(I_MOV, new CArgument(TRegister(i)),
                                               replace[i]);
            n->prev = start->prev;
            n->next = start;
            start->prev->next = n;
            start->prev = n;            
        }

    /* Nun die Argumente ersetzen */
    while(start != end) {
        if(start->opsize != 1)
            for(int i = 0; i < changeable_args[start->insn]; i++)
                if(start->args[i])
                    for(int reg = rAX; reg <= rDI; reg++)
                        if(compare[reg]) {
                            if(*start->args[i] == *compare[reg])
                                replace_argument(start->args[i], TRegister(reg), use_adr[reg]);
                            else if(use_adr[reg]) {
                                int d = compare[reg]->adr_diff(start->args[i]);
                                if(d && d > -127 && d < 127) {
                                    replace_argument(start->args[i], TRegister(reg), use_adr[reg]);
                                    start->args[i]->inc_imm(-d);
                                }
                            }
                        }
        start = start->next;
    }
}

#ifdef GLOBAL_ALLOC
/*
 *  Sucht Operand, der am besten geeignet ist, global in ein Register
 *  gepackt zu werden.
 */
TEstimate* global_find_best(int min_bp, char* mask)
{
    TEstimate* p = estimated_savings;
    TEstimate* best = 0;
    int best_savings = 0;
    while(p) {
        if(p->arg->type == CArgument::MEMORY
           && p->arg->memory[0] == rBP
           && p->save_if_value > best_savings
           && mask[p->arg->immediate - min_bp] != 2
           && mask[p->arg->immediate - min_bp + 1] != 2
           && !p->used)
        {
            best_savings = p->save_if_value;
            best = p;
        }
        p = p->next;
    }
    return best;
}

void global_replace(CInstruction* insn, CArgument* a, TRegister r)
{
    while(insn) {
        for(int i = 0; i < changeable_args[insn->insn]; i++)
            if(insn->args[i] && *insn->args[i] == *a) {
                delete insn->args[i];
                insn->args[i] = new CArgument(r);
                changed = true;
            }            
        insn = insn->next;
    }
}

/*
 *  Globale Register-Allokation. Versucht, Speichervariablen in
 *  Register zu packen.
 */
void global_register_allocation(CInstruction* insn, bool* regs_used)
{
    CInstruction* pre = 0;
    clear_estimates();

    /* Stackframe überlesen */
    if(insn->insn == I_ENTER) {
        pre = insn;
    } else if(insn->insn == I_PUSH && insn->args[0]->is_reg(rBP)) {
        insn = insn->next;
        if(insn->insn == I_MOV && insn->args[0]->is_reg(rBP) &&
           insn->args[1]->is_reg(rSP))
            pre = insn;
    }
    /* eliminate stack checking code */
    if(pre && pre->next && pre->next->next && pre->next->next->next) {
        CInstruction* movax = pre->next;
        CInstruction* call = movax->next;
        CInstruction* subsp = call->next;
        if(movax->insn == I_MOV && movax->args[0]->is_reg(rAX) &&
           call->insn == I_CALLF &&
           subsp->insn == I_SUB && subsp->args[0]->is_reg(rSP) &&
           *subsp->args[1] == *movax->args[1])
            pre = subsp;
    }
    if(!pre)
        return;
    
    insn = pre->next;

    int last_ip = -1;   
    for(CInstruction* p = insn; p != 0; p = p->next)
        if(p->opsize == 2) {
            estimate_insn(p, last_ip == p->ip);
            last_ip = p->ip;
        }

    /* `krumme' Operanden wegwerfen */
    int min_bp = 0;
    int max_bp = 0;
    TEstimate* pe = estimated_savings;
    estimated_savings = 0;
    while(pe) {
        TEstimate* q = pe->next;
        if(pe->arg->type != CArgument::MEMORY || pe->arg->memory[0] != rBP
           || (pe->arg->immediate & 1) != 0) {
            delete pe;
        } else {
            if(pe->arg->immediate < min_bp)
                min_bp = pe->arg->immediate;
            if(pe->arg->immediate > max_bp)
                max_bp = pe->arg->immediate;
            pe->next = estimated_savings;
            estimated_savings = pe;
        }
        pe = q;
    }

    /* Aliase rauswerfen */
    max_bp += 3;                // Max. Operandengröße 4
    char* markers = new char[max_bp - min_bp + 1];
    for(int i = 0; i <= max_bp - min_bp; i++)
        markers[i] = 0;
    for(pe = estimated_savings; pe; pe = pe->next) {
        int i = pe->arg->immediate - min_bp;
        markers[i] = markers[i+1] = 1;

        if(pe->arg->immediate < 0)
            pe->save_if_value += 1 + mem_op_size(pe->arg);
    }

    for(CInstruction* p = insn; p; p = p->next) {
        for(int i = 0; i < 3; i++)
            if(p->args[i] && p->args[i]->type == CArgument::MEMORY
               && p->args[i]->memory[0] == rBP) {
                int j = p->args[i]->immediate;
                int count = 0;
                if(p->insn == I_LES || p->insn == I_LDS
                   || p->insn == I_CALLF || p->insn == I_JMPF)
                    count = 4;
                else if(p->opsize == 1)
                    count = 1;
                else if((j & 1) != 0)
                    count = 2;
                
                for(int q = 0; q < count; q++) {
                    if(j >= min_bp && j <= max_bp)
                        markers[j - min_bp] = 2;
                    j++;
                }
            }
    }

    /* Zu ersetzende Operanden suchen */
    for(int r = rAX; r <= rDI; r++)
        if(!regs_used[r]) {
            TEstimate* p = global_find_best(min_bp, markers);
            if(p) {
                CArgument* arg = new CArgument(*p->arg);
                p->used = true;
                regs_used[r] = true;
                /* Alle Vorkommnisse von /p->arg/ durch /r/ ersetzen */
                if(p->arg->immediate > 0) {
                    /* is'n Parameter */
                    CInstruction* i = new CInstruction(I_MOV,
                                                       new CArgument(TRegister(r)),
                                                       new CArgument(*arg));
                    i->next = pre->next;
                    i->prev = pre;
                    i->next->prev = i;
                    pre->next = i;
                    changed = true;
                }
                global_replace(insn, arg, TRegister(r));
                delete arg;
            } else
                break;
        }

    delete[] markers;
    clear_estimates();
}
#endif

/*
 *  Registerallokation durchführen
 *
 *  Wenn eine Funktion ein Register nicht verwendet, kann dieses
 *  innerhalb eines `basic blocks'
 *  - eine Konstante
 *  - eine Adresse
 *  - einen Speicherwert
 *  aufnehmen. Basic Blocks sind die Bereiche zwischen einem Label
 *  oder Transfer in Unterprogramm und dem Label/Transfer.
 */
void register_allocation(CInstruction* oinsn)
{
    /* Suche unbenutzte Register */
    bool regs_used[rMAX];
    bool can_global = true;
    int stackcheck = 0;

    for(int i = 0; i < rMAX; i++)
        regs_used[i] = false;
    regs_used[rSS] = regs_used[rDS] = regs_used[rSP] = regs_used[rES]
        = regs_used[rBP] = regs_used[rCS] = true;

    CInstruction* insn = oinsn->next;          // der erste Befehl kann nicht Teil eines Blocks sein
    if(oinsn->insn == I_PUSH)
        stackcheck = 1;
    for(CInstruction* p = insn; p; p = p->next) {
        switch(p->insn) {
         case I_MOV:
            if(stackcheck == 1 || stackcheck == 2) 
                stackcheck++;
            else
                stackcheck = -1;
            break;
         case I_CALLF:
            if(stackcheck == 3 && p->next && p->next->insn == I_SUB)
                break;
         case I_LEA:
         case I_CALLN:
         case I_STRING:
         case I_JMPF:
            can_global = false;
            stackcheck = 0;
            break;
         default:
            stackcheck = 0;
        }
        for(int i = 0; i < 3; i++)
            if(p->args[i])
                switch(p->args[i]->type) {
                 case CArgument::REGISTER:
                    regs_used[p->args[i]->reg] = true;
                    break;
                 case CArgument::MEMORY:
                    regs_used[p->args[i]->segment] = true;
                    regs_used[p->args[i]->memory[0]] = true;
                    regs_used[p->args[i]->memory[1]] = true;
                    break;
                 default:;
                }
        if(p->insn == I_MUL || p->insn == I_DIV || p->insn == I_IMUL || p->insn == I_IDIV)
            regs_used[rAX] = regs_used[rDX] = true;
    }

    /* Aliasregister auflösen */
    for(int i = rNONE; i < rMAX; i++)
        if(regs_used[i])
            for(int j = rNONE; j < rMAX; j++)
                if(alias_reg(TRegister(i), TRegister(j)))
                    regs_used[j] = true;

    /* haben wir freie Register? */
    int free_regs = 0;
    for(int i = rAX; i <= rDI; i++)
        if(!regs_used[i])
            free_regs++;
    
    if(!free_regs)
        return;

#ifdef GLOBAL_ALLOC
    /* können wir global Variablen in Register packen? */
    if(can_global) {
        global_register_allocation(oinsn, regs_used);
    }
#endif

    /* ok, suche basic blocks */
    CInstruction* start = insn;
    int last_ip = -1;
    while(insn) {
        if(insn->opsize != 1) switch(insn->insn) {
         case I_LABEL: case I_CALLF: case I_CALLN:
         case I_JMPF:  case I_RETN:  case I_RETF:
         case I_STRING:
            /* Ende des Blocks */
            optimize_basic_block(start, insn, regs_used);
            start = insn->next;
            last_ip = -1;
            break;
         default:
            estimate_insn(insn, last_ip == insn->ip);
            last_ip = insn->ip;
            break;
        }
        insn = insn->next;
    }
    clear_estimates();
}
