/*
 *  Common Subexpression Elimination
 *
 *  (c) copyright 2000,2001 Stefan Reuther
 *
 *  Verfahren:
 *  - innerhalb eines basic blocks
 *    Suche Abschnitt der Form
 *       <codeA>
 *       <codeB>
 *       <codeA>
 *    wobei <codeB> und <codeA> sich nicht beeinflussen. Lösche zweiten
 *    <codeA>.
 */
#include <vector>
#include "cse.h"
#include "optimize.h"
#include "dfa.h"
#include "tpufmt.h"

using std::cout;
using std::cerr;
using std::endl;

/* [OBSOLETE]
#define IFDEBUG if(debugflag)
bool debugflag = 0;
*/
#define IFDEBUG if(0)

/* Registerbeeinflussung updaten (Byteregister beeinflussen ihre
   Hauptregister, und andersrum) */
void OperandSet::fix_regs()
{
#define DREG(name,hi,lo)               \
    if(regs & (1 << name))             \
        regs |= (1 << hi) | (1 << lo); \
    if(regs & ((1 << hi) | (1 << lo))) \
        regs |= (1 << name);

    DREG(rAX, rAH, rAL);
    DREG(rBX, rBH, rBL);
    DREG(rCX, rCH, rCL);
    DREG(rDX, rDH, rDL);
#undef DREG

    regs &= ~(1 << rNONE);
}

/* konservative Form von fix_regs: ein Register zählt nicht als
   überschrieben, wenn nur ein Teil überschrieben wurde */
void OperandSet::fix_regs_conservative()
{
#define DREG(name,hi,lo)                                        \
    if(regs & (1 << name))                                      \
        regs |= (1 << hi) | (1 << lo);                          \
    if((regs & ((1 << hi)|(1 << lo))) == ((1 << hi)|(1 << lo))) \
        regs |= (1 << name);

    DREG(rAX, rAH, rAL);
    DREG(rBX, rBH, rBL);
    DREG(rCX, rCH, rCL);
    DREG(rDX, rDH, rDL);
#undef DREG

    regs &= ~(1 << rNONE);
}

/* Operand /a/ aufnehmen. Register, die durch Zugriff auf den
   Operanden gelesen werden, in /read/ markieren */
void OperandSet::add_op(CArgument* a, OperandSet& read)
{
    if(!a)
        return;

    IFDEBUG {
        cout << this << "->add_op(";
        a->print(cout);
        cout << ")" << endl;
    }

    switch(a->type) {
     case CArgument::REGISTER:
        regs |= (1 << a->reg);
        break;
     case CArgument::MEMORY:
        read.regs |= (1 << a->memory[0]);
        read.regs |= (1 << a->memory[1]);
        read.regs |= (1 << a->segment);
        if(!contains_arg(a))
            mem.push_back(a);
        break;
     case CArgument::IMMEDIATE:
     case CArgument::LABEL:
        break;
    }
}

/* true wenn Speicherargument /a/ enthalten */
bool OperandSet::contains_arg(CArgument* a)
{
    for(vector<CArgument*>::iterator j = mem.begin(); j != mem.end(); ++j)
        if(*a == **j)
            return true;
    return false;
}

std::ostream& operator<<(std::ostream& os, const OperandSet& set)
{
    char b = '[';
#define OUT(x) (os << b << (x), b = ',')
    if (set.stack)
        OUT("stack");
    if (set.flags)
        OUT("flags");
    for (int i = rNONE; i < rMAX; ++i)
        if (set.regs & (1 << i))
            OUT(reg_names[i]);
    for (unsigned i = 0; i < set.mem.size(); ++i) {
        os << b;
        set.mem[i]->print(os);
        b = ',';
    }
#undef OUT
    if (b == '[')
        os << "[]";
    else
        os << ']';
    return os;
}

/* true wenn p eine Trennung zw. basic blocks ist */
bool is_break(CInstruction* p)
{
    switch(p->insn) {
     case I_CALLF:
        if (is_call_to(p->args[0], SYS_LONG_DIV) || is_call_to(p->args[0], SYS_LONG_MUL))
            return false;
        return true;
     case I_INVALID:
     case I_LABEL:
     case I_CALLN:
     case I_STRING:
     case I_IN:
     case I_OUT:                // handle these like calls
        return true;
     default:
        return false;
    }
}

/* Abhängigkeiten von Befehl p berechnen */
void compute_insn_dep(OperandSet& in, OperandSet& out, CInstruction* p)
{
    switch(p->insn) {
     case I_CALLF:
        if (is_call_to(p->args[0], SYS_LONG_DIV) || is_call_to(p->args[0], SYS_LONG_MUL)) {
            in.regs |= (1 << rAX) | (1 << rBX) | (1 << rCX) | (1 << rDX);
            out.regs |= (1 << rAX) | (1 << rBX) | (1 << rCX) | (1 << rDX) | (1 << rSI) | (1 << rDI);
            out.flags = true;
            break;
        }
        /* FALLTHROUGH */
     case I_INVALID:
     case I_LABEL:
     case I_CALLN:
        cerr << "should not happen" << endl;
        break;
     case I_MOV:
     case I_LEA:
        in.add_op(p->args[1]);
        out.add_op(p->args[0], in);
        break;
     case I_LES:
        in.add_op(p->args[1]);
        out.add_op(p->args[0], in);
        out.regs |= 1 << rES;
        break;
     case I_LDS:
        in.add_op(p->args[1]);
        out.add_op(p->args[0], in);
        out.regs |= 1 << rDS;
     case I_XCHG:
        in.add_op(p->args[1]);
        in.add_op(p->args[0]);
        out.add_op(p->args[1], in);
        out.add_op(p->args[0], in);
        break;
     case I_POP:
        out.add_op(p->args[0], in);
        in.stack = true;
        break;
     case I_PUSH:
        in.add_op(p->args[0]);
        in.stack = true;
        break;
     case I_POPF:
        out.stack = true;
        break;
     case I_PUSHF:
        in.flags = true;
        in.stack = true;
        break;
     case I_POPA:
        out.regs |= (1 << rAX) | (1 << rBX) | (1 << rCX) | (1 << rDX)
                  | (1 << rSP) | (1 << rBP) | (1 << rSI) | (1 << rDI);
        break;
     case I_PUSHA:
        in.regs |= (1 << rAX) | (1 << rBX) | (1 << rCX) | (1 << rDX)
                 | (1 << rSP) | (1 << rBP) | (1 << rSI) | (1 << rDI);
        break;
     case I_DEC:
     case I_INC:
     case I_NOT:
     case I_NEG:
        in.add_op(p->args[0]);
        out.add_op(p->args[0], in);
        out.flags = true;
        break;
     case I_BCD:
        if(p->opsize == 1) {
            in.regs |= (1 << rAL);
            out.regs |= (1 << rAL);
        } else {
            in.regs |= (1 << rAX);
            out.regs |= (1 << rAX);
        }
        out.flags = true;
        break;
     case I_SBB:
     case I_ADC:
     case I_RCL:
     case I_RCR:
        in.flags = true;
        /* FALLTHROUGH */
     case I_ROL:
     case I_ROR:
     case I_SHL:
     case I_SHR:
     case I_SAR:
     case I_ADD:
     case I_OR:
     case I_AND:
     case I_SUB:
     case I_XOR:
        out.add_op(p->args[0], in);
        /* FALLTHROUGH */
     case I_CMP:
     case I_TEST:
        in.add_op(p->args[0]);
        in.add_op(p->args[1]);
        out.flags = true;
        break;
     case I_IMUL:
        if(p->args[2]) {
            in.add_op(p->args[1]);
            in.add_op(p->args[2]);
            out.add_op(p->args[0], in);
            out.flags = true;
            break;
        }
        /* FALLTHROUGH */
     case I_MUL:
        in.add_op(p->args[0]);
        out.regs |= (1 << rAX);
        if(p->opsize == 1) {
            in.regs |= (1 << rAL);
        } else {
            in.regs |= (1 << rAX);
            out.regs |= (1 << rDX);
        }
        out.flags = true;
        break;
     case I_DIV:
     case I_IDIV:
        in.add_op(p->args[0]);
        in.regs |= (1 << rAX);
        out.regs |= (1 << rAX);
        if(p->opsize == 2) {
            in.regs |= (1 << rDX);
            out.regs |= (1 << rDX);
        }
        out.flags = true;
        break;
     case I_JCC:
        in.flags = true;
        break;
     case I_JMPN:
     case I_JMPF:
     case I_RETN:
     case I_RETF:
        break;
     case I_LOOP:
        if(p->param != 2)
            in.flags = true;
        out.regs |= (1 << rCX);
        /* FALLTHROUGH */
     case I_JCXZ:
        in.regs |= (1 << rCX);
        break;
     case I_CBW:
        in.regs |= (1 << rAL);
        out.regs |= (1 << rAX);
        break;
     case I_CWD:
        in.regs |= (1 << rAX);
        out.regs |= (1 << rAX) | (1 << rDX);
        break;
     case I_XLAT:
        in.regs |= (1 << rAL) | (1 << rBX);
        out.regs |= (1 << rAL);
        break;
     case I_ENTER:
     case I_LEAVE:
        in.regs |= (1 << rSP) | (1 << rBP);
        out.regs |= (1 << rSP) | (1 << rBP);
        break;
     case I_SETCC:
        in.flags = true;
        out.add_op(p->args[0], in);
        break;
     case I_SETALC:
        in.flags = true;
        out.regs |= (1 << rAL);
        break;
     case I_STRING:
        // FIXME: this does not happen
        break;
     case I_FLAG:
        out.flags = true;
        break;
     case I_LAHF:
        in.flags = true;
        out.regs |= (1 << rAH);
        break;
     case I_SAHF:
        in.regs |= (1 << rAH);
        out.flags = true;
        break;
     case I_IN:
        in.add_op(p->args[1]);
        out.add_op(p->args[0], in);
        break;
     case I_OUT:
        in.add_op(p->args[0]);
        in.add_op(p->args[1]);
        break;
     default:
        break;
    }
}

/* Abhängigkeiten berechnen
   Dumbfire-Version: in==gelesene Daten, out==geschriebene Daten */
void compute_dependencies(OperandSet& in, OperandSet& out,
                          CInstruction* p, CInstruction* end)
{
    while(p != end) {
        compute_insn_dep(in, out, p);
        p = p->next;
    }
}

void merge_os(OperandSet& a, const OperandSet& b)
{
    a.regs |= b.regs;
    a.flags |= b.flags;
    a.stack |= b.stack;
    for(vector<CArgument*>::const_iterator i = b.mem.begin(); i != b.mem.end(); i++)
        if(!a.contains_arg(*i))
            a.mem.push_back(*i);
}

/* Abhängigkeiten berechnen
   intelligentere Version
   erzeugte Werte stehen nicht unter /in/
   (e.g., bei MOV AX,3 / ADD AX,2 ist AX kein Eingaberegister) */
void compute_dependencies_int(OperandSet& in, OperandSet& out,
                              CInstruction* p, CInstruction* end)
{
    if(p == end)
        return;
    compute_dependencies_int(in, out, p->next, end);

    OperandSet this_out;
    OperandSet this_in;

    compute_insn_dep(this_in, this_out, p);
/* [OBSOLETE]
    out = out + this_out;
    in = (in - this_out) + this_in;
*/

    in.regs &= ~this_out.regs;
    in.flags &= !this_out.flags;
    for(vector<CArgument*>::iterator i = this_out.mem.begin(); i != this_out.mem.end(); ++i)
        for(vector<CArgument*>::iterator j = in.mem.begin(); j != in.mem.end(); ++j)
            if(**i == **j) {
                in.mem.erase(j);
                break;
            }

    merge_os(out, this_out);
    merge_os(in, this_in);
}

/* true, wenn a und b disjunkt sind */
bool check_dependencies(OperandSet& a, OperandSet& b)
{
    if(a.regs & b.regs)
        return false;
    if(a.flags & b.flags)
        return false;
    for(vector<CArgument*>::iterator i = a.mem.begin(); i != a.mem.end(); ++i)
        if(b.contains_arg(*i))
            return false;
    return true;
}

/* CSE durchführen, wobei a1,a2->Anfang der <codeA>-Stücken */
/* Precondition: *a1 == *a2 */
/* les==true -> a1 ist `LES foo, bar' und a2 ist `MOV foo, bar'
   diese werden als identisch betrachtet. Das sollte keine Probleme
   geben; die entsprechende Sequenz `MOV ES, bar[2]; MOV foo, bar'
   am Anfang würde exakt gleich optimiert. (der Parameter les hat
   dz. keinen Einfluss) */
bool try_cse(CInstruction* a1, CInstruction* a2, bool les)
{
    CInstruction* b = a1->next;
    CInstruction* a2end = a2->next;
    CInstruction* maxb = 0;
    CInstruction* a2endmax = 0;
    /* <codeA> == [a1,b)
       <codeB> == [b,a2)
       <codeA> == [a2,a2end) */
    bool had_call=0;                /* DEBUGGING */

    while(1) {
        if (b->insn == I_CALLF)     /* DEBUGGING */
            had_call = true;
        OperandSet codea_read, codea_write;
        OperandSet codeb_read, codeb_write;
        /* ist die gegebene Situation gültig? */
        compute_dependencies_int(codea_read, codea_write, a1, b);
        compute_dependencies(codeb_read, codeb_write, b, a2);
        codea_read.fix_regs();
        codea_write.fix_regs();
        codeb_read.fix_regs();
        codeb_write.fix_regs();
        /* codeB darf keine Dinge modifizieren, die codeA braucht */
        if(!codea_read.stack) {
            /* wenn die Flags egal sind ... */
            if(codea_write.flags && codeb_write.flags
               && !codeb_read.flags && !codea_read.flags && flag_check(a2end)) {
                codea_write.flags = codeb_write.flags = false;
            }
/* [DEBUG]
if(had_call)
cout << "\naread=" << codea_read
     << "\nawrite=" << codea_write
     << "\nbwrite=" << codeb_write
     << "\ncheck_dep(aread, awrite) = " << check_dependencies(codea_read, codea_write)
     << "\ncheck_dep(aread, bwrite) = " << check_dependencies(codea_read, codeb_write)
     << "\ncheck_dep(awrite, bwrite) = " << check_dependencies(codea_write, codeb_write)
     << "\n";
*/

            if(check_dependencies(codea_read, codea_write)
               && check_dependencies(codea_read, codeb_write)
               && check_dependencies(codea_write, codeb_write)) {
                /* OK -> merken */
                maxb = b;
                a2endmax = a2end;
            }
        }
        if(b == a2 || !a2end)
            break;
        if(!(*a2end == *b))
            break;
        b = b->next;
        a2end = a2end->next;
    }

    if(maxb) {
        /* wir können was löschen */
        if (had_call)      /* DEBUGGING */
            cout << "@";
/* [DEBUG]
        cout << endl << endl << "<<< CSE Result >>>" << endl;

        for(CInstruction* i = a1; i != maxb; i = i->next) {
            cout << "A-keep\t";
            i->print(cout);
        }
        for(CInstruction* i = maxb; i != a2; i = i->next) {
            cout << "B-keep\t";
            i->print(cout);
        }
        for(CInstruction* i = a2; i != a2endmax; i = i->next) {
            cout << "A-del\t";
            i->print(cout);
        }
        cout << endl << endl;
*/

        /* löschen */
        CInstruction* p = a1;
        while(p->next != a2)
            p = p->next;
        while(p->next != a2endmax) {
            CInstruction* q = p->next;
            p->next = q->next;
            delete q;
        }
        changed = true;

        return true;
    }
    return false;
}

CInstruction* do_cse_once(CInstruction* insn)
{
    /* insn == Anfang Code A */
    CInstruction* a2 = insn->next;
    if(is_break(insn))
        return insn->next;
    while(a2) {
        if(is_break(a2))
            return insn;
        if(*insn == *a2) {
            if(try_cse(insn, a2, false))
                return insn;
        } else if(insn->insn == I_LES && a2->insn == I_MOV
                  && *insn->args[0] == *a2->args[0]
                  && *insn->args[1] == *a2->args[1]) {
            if(try_cse(insn, a2, true))
                return insn;
        }
        a2 = a2->next;
    }
    return insn;
}

/* Hauptroutine CSE */
void do_cse(CInstruction* insn)
{
    while(insn) {
        CInstruction* i = do_cse_once(insn);
        if(i == insn)
            insn = insn->next;
        else
            insn = i;
    }
}
