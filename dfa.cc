/*
 *  "Datenflußanalyse" für tputuner
 *
 *  (c) copyright 1998,1999,2000 by Stefan Reuther
 *
 *  Der Code wird analysiert und Ladebefehle protokolliert. Wenn
 *  Werte mehrfach geladen werden, wird der Original-Ladebefehl
 *  verworfen.
 */
#include "insn.h"
#include "dfa.h"
#include "optimize.h"
#include "global.h"

/*
 *  Repräsentation eines Register/Stack-Wertes
 */
struct TValue {
    typedef enum { UNKNOWN, CONSTANT, MEMORY, SEGMENT } TType;
    TValue* next;       /* für Stack */

    TType type;
    CArgument* value;   /* CONSTANT -> das Argument, das in das Register */
                    	/* geladen wurde */
                        /* MEMORY -> der Operand, ... */
                        /* SEGMENT -> der Operand von LES */
    CInstruction* insn; /* der Befehl, der diesen Wert hier rein geladen hat */

    bool used;    // true -> Wert wird benutzt
    bool known;   // true -> Benutzung ist bekannt

    TValue();
    void clear(bool safe=false);       // auf `undefiniert' setzen
    void killing(bool safe=false);     // wenn dieses Objekt überschrieben wird
    void kill_insn();   // erzeugenden Befehl löschen
    TValue& operator=(TValue& c);
    void set_const(CInstruction* i, CArgument* a);
    void set_mem(CInstruction* i, CArgument* a);
    void swap(TValue& o);
};

TValue values[rMAX];
TValue* stack;

int cur_ip;

TValue::TValue()
    : type(UNKNOWN), value(0), insn(0), used(false), known(false)
{ }

/*
 *  Setzt den Wert auf `unbekannt'
 *  safe==true -> es ist bekannt, warum das Register geändert wurde
 *  safe==false -> das Register ist möglicherweise Funktionsparameter
 */
void TValue::clear(bool safe)
{
    killing(safe);
    type = UNKNOWN;
    used = known = false;
    value = 0;
}

/*
 *  Löscht den Befehl, der diesen Wert erzeugte
 */
void TValue::kill_insn()
{
    if(!insn) return;
    if(!insn || !insn->prev || !insn->next) return;
    CInstruction* i = insn;
    insn->prev->next = insn->next;
    insn->next->prev = insn->prev;
    delete i;
    changed = true;
}

/*
 *  wird aufgerufen, wenn dieser Wert überschrieben wird.
 *  safe = true --> Löschen des erzeugenden Befehls ist sicher,
 *         da die Verwendung bekannt ist
 *  safe = false --> Unsicher; es wird ein Transferbefehl
 *         benutzt, der u.U. Registerparameter nimmt
 */
void TValue::killing(bool safe)
{
    if(used || (!known && !safe) || type==UNKNOWN || type==SEGMENT) return;
//     cout << "cur_ip=" << cur_ip << " uks=" << used << known << safe;
//     if(insn) cout << " ip=" << hex << insn->ip << dec;
//     cout << endl;
    /* Der Befehl, der diesen Wert erzeugte, wird nicht benötigt,
     * da der Wert nicht benutzt wird -> Löschen */
    kill_insn();
}

template<class T>
inline void xchg(T& a, T& b)
{
    T c = a;
    a = b;
    b = c;
}

/*
 *  Zwei Werte tauschen. Ändert nichts bzgl. used, known etc.
 *  *NUR* für die Implementierung von `xchg' gedacht.
 */
void TValue::swap(TValue& o)
{
    xchg(type, o.type);
    xchg(value, o.value);
    xchg(insn, o.insn);
    xchg(used, o.used);
    xchg(known, o.known);
}

/*
 *  Nutzen bei Zuweisung `mov ax,bx'
 */
TValue& TValue::operator=(TValue& c)
{
    if(&c == this) return *this;
    
    killing(true);
    next = 0;
    type = c.type;
    value = c.value;
    insn = c.insn = 0;  /* verhindert Löschung des Befehls */
    used = known = false;
    c.used = c.known = true;
    return *this;
}

/*
 *  Zuweisung `mov ax,n'
 */
void TValue::set_const(CInstruction* i, CArgument* a)
{
    killing(true);
    type = CONSTANT;
    value = a;
    insn = i;
    used = known = false;
}

/*
 *  Zuweisung `mov ax,[mem]'
 */
void TValue::set_mem(CInstruction* i, CArgument* a)
{
    killing(true);
    type = MEMORY;
    value = a;
    insn = i;
    used = known = false;
}

/*
 *  CValueIterator iteriert über alle Werte
 *  nicht STL-Iterator kompatibel
 */
class CValueIterator {
    typedef enum { STARTING, REGISTERS, STACK } TStatus;
    TStatus status;
    TRegister reg;
    TValue* v;
 public:
    CValueIterator();
    TValue* get_next();
};

CValueIterator::CValueIterator()
    : status(STARTING)
{}

TValue* CValueIterator::get_next()
{
    if(status==STARTING) {
	reg = rAX;
	status = REGISTERS;
	return &values[reg];
    }
    if(status==REGISTERS) {
	reg = TRegister(reg + 1);
	if(reg==rMAX) {
	    status = STACK;
	    v = stack;
	    return v;
	} else {
	    return &values[reg];
	}
    }
    if(status==STACK && v) {
	v = v->next;
	return v;
    }
    return 0;
}

void set_unknown_regs()
{
    for(int i=0; i<rMAX; i++)
	values[i].clear();
}

void set_unknown_stack()
{
    while(stack) {
	TValue* p = stack;
	stack = stack->next;
	delete p;
    }
}

/*
 *  Transfer / anderer Befehl mit unbekannten Auswirkungen
 */
void set_unknown()
{
    set_unknown_regs();
    set_unknown_stack();
}

/*
 *  Registerwert auf unbekannt setzen
 *  safe=true  -> ich weiß, warum das geschah
 *       false -> Register ist möglicherweise Eingabeparameter
 */
void set_unknown(TRegister reg, bool safe=false)
{
    values[reg].clear(safe);
    switch(reg) {
     case rAX:
	values[rAL].clear(safe);
	values[rAH].clear(safe);
	break;
     case rBX:
	values[rBL].clear(safe);
	values[rBH].clear(safe);
	break;
     case rCX:
	values[rCL].clear(safe);
	values[rCH].clear(safe);
	break;
     case rDX:
	values[rDL].clear(safe);
	values[rDH].clear(safe);
	break;
     case rAL:
     case rAH:
	values[rAX].clear(safe);
	break;
     case rBL:
     case rBH:
	values[rBX].clear(safe);
	break;
     case rCL:
     case rCH:
	values[rCX].clear(safe);
	break;
     case rDL:
     case rDH:
	values[rDX].clear(safe);
	break;
     default:
	break;
    }
}

/*
 * Ein Register als benutzt markieren
 */
void use_reg(TRegister reg)
{
    values[reg].used = true;
    switch(reg) {
     case rAX:
	values[rAL].used = true;
	values[rAH].used = true;
	break;
     case rBX:
	values[rBL].used = true;
	values[rBH].used = true;
	break;
     case rCX:
	values[rCL].used = true;
	values[rCH].used = true;
	break;
     case rDX:
	values[rDL].used = true;
	values[rDH].used = true;
	break;
     case rAL:
     case rAH:
	values[rAX].used = true;
	break;
     case rBL:
     case rBH:
	values[rBX].used = true;
	break;
     case rCL:
     case rCH:
	values[rCX].used = true;
	break;
     case rDL:
     case rDH:
	values[rDX].used = true;
	break;
     default:
	break;
    }
}

/*
 *  Stackverwaltung
 *  push_value(0) = push unbekannten Wert
 */
void push_value(TValue* v = 0)
{
    if(!v) v = new TValue;
    v->next = stack;
    stack = v;
}

/*
 *  Liefert und entfernt top-of-stack
 *  0 = Stack ist leer
 */
TValue* pop_value()
{
    if(!stack) return 0;
    TValue* v = stack;
    stack=stack->next;
    return v;
}

/*
 *  prev-Zeiger erzeugen
 *  Nur die DFA sorgt für konsistente prev-Zeiger, den anderen
 *  Modulen ist das egal
 */
void make_backlinks(CInstruction* insn)
{
    CInstruction* prev = 0;
    while(insn) {
	insn->prev = prev;
	prev = insn;
	insn = insn->next;
    }
}

/*
 *  Speicheroperand als benutzt markieren
 */
void use_mem(CArgument* a)
{
    if(a->type != CArgument::MEMORY) return;
    if(a->memory[0] != rNONE) values[a->memory[0]].used = true;
    if(a->memory[1] != rNONE) values[a->memory[1]].used = true;

    TRegister def_seg = (a->uses_reg(rBP) ? rSS : rDS);
    if(a->segment != def_seg && values[def_seg].type != TValue::UNKNOWN
       && values[a->segment].type == values[def_seg].type
       && *values[a->segment].value == *values[def_seg].value)
    {
        values[a->segment].known = true;
        values[def_seg].used = true;
        a->segment = def_seg;
        changed = true;
    } else
        values[a->segment].used = true;
}

/*
 *  Ein Argument als benutzt markieren (`auslesen')
 */
void use_argument(CArgument* a)
{
    if(a->type == CArgument::REGISTER) {
	use_reg(a->reg);
    } else if(a->type == CArgument::MEMORY)
	use_mem(a);
}

/*
 *  true, wenn das Argument ein `sicherer' Speicheroperand ist
 *  (zwei gleiche Referenzen bedeuten dasselbe)
 */
bool is_safe_mem(CArgument* a)
{
    if(a->type != CArgument::MEMORY) return false;
    /* nur Referenzen via BP sind `immer sicher' */
    if(a->memory[0] != rNONE && a->memory[0] != rBP) return false;
    if(a->memory[1] != rNONE && a->memory[1] != rBP) return false;
    /* nur diese Segmente sind konstant */
    if(a->segment != rDS && a->segment != rCS && a->segment != rSS)
	return false;
    return true;
}

/*
 *  Zerstört einen Wert, der in einem Befehl als Ziel verwendet wird
 *  use_it = Argument ist gleichzeitig Quelloperand
 */
void destroy_argument(CArgument* a, bool use_it)
{
    if(a->type == CArgument::REGISTER) {
	if(use_it) use_reg(a->reg);
	set_unknown(a->reg, true);
    } else if(a->type == CArgument::MEMORY) {
	CValueIterator vi;
	TValue* v;
	while((v = vi.get_next())) {
	    if(v->value && *v->value == *a) {
		v->insn = 0;
		v->clear();
	    }
	}
	use_mem(a);
    }
}

/*
 *  Optimiert einen (Quell)Operanden
 *  allow_const = true -> erlaube auch konstante Werte
 */
void optimize_argument(int os, CArgument*& a, bool allow_const)
{
    if(os==2 && (is_safe_mem(a) || a->type==CArgument::IMMEDIATE)) {
	for(int i=rAX; i<=rDI; i++) {
	    if((values[i].type==TValue::MEMORY/* || values[i].type==TValue::CONSTANT*/)
               && *values[i].value==*a) {
		/* es ist ein Speicheroperand, den wir schon in einem Register
		 * haben */
		delete a;
		a = new CArgument(TRegister(i));
		changed = true;
	    }
	}
    } else
	use_argument(a);
}

/*
 *  Prüft, ob insn die Flags ändern darf, ohne in die Programmlogik
 *  einzugreifen
 */
bool flag_check(CInstruction* insn)
{
    while(insn) {
	switch(insn->insn) {
         case I_DEC: case I_INC: case I_ADD: case I_OR:
         case I_AND: case I_SUB:
         case I_XOR: case I_CMP: case I_IMUL:case I_MUL:
         case I_DIV: case I_IDIV: case I_NOT: case I_NEG:
	    return true;
         case I_ADC:
         case I_SBB:
         case I_JCC:
         case I_SETCC:
	    return false;
         case I_JMPF:
         case I_JMPN:
            /* better safe than sorry. Happens when CSE/early-jump
               combines two conditional blocks (ChartUsr::RecalcShip) */
            return false;
         default:
	    break;
	}
	insn = insn->next;
    }
    return true;
}

TRegister find_reg_with_value(CArgument* arg)
{
    for(int i=rAX; i<=rDI; i++) {
        if(values[i].type == TValue::CONSTANT
           && *values[i].value == *arg)
            return TRegister(i);
    }
    return rNONE;
}

/*
 *  Optimiert MOV-Insns
 */
CInstruction* optimize_mov(CInstruction* insn)
{
    if(insn->args[0]->is_word_reg()) {
	if(insn->args[1]->is_word_reg()) {
	    /*
	     * mov reg,reg
	     */
	    if(insn->args[0]->reg != insn->args[1]->reg)
		values[insn->args[0]->reg] = values[insn->args[1]->reg];
	    return insn;
	} else if(insn->args[1]->type == CArgument::IMMEDIATE) {
	    /*
	     * mov reg,imm
	     */
	    TRegister reg = insn->args[0]->reg;
	    /* haben wir den Wert schon in einem anderen Register? */
            TRegister r1 = find_reg_with_value(insn->args[1]);
            if(r1 != rNONE) {
                delete insn->args[1];
                insn->args[1] = new CArgument(r1);
                changed = true;
                if(r1 != reg)
                    values[reg] = values[r1];
                return insn;
	    }
                
 	    /* haben wir einen ähnlichen Wert in diesem Register? */
 	    if(values[reg].type == TValue::CONSTANT
	       && values[reg].value->reloc==0 && insn->args[1]->reloc==0) {
		/* mov reg,(echte Konstante) */
		/* wobei der vorige Wert ebenfalls eine Konstante ist */
		int difference = insn->args[1]->immediate - values[reg].value->immediate;
		if(flag_check(insn)) {
		    if(difference==1 || difference==2) {
			/* Änderung um 1 oder 2 durch INC ausgleichen */
			insn->insn = I_INC;
			changed = true;
			if(difference==2) {
			    CInstruction* i = new CInstruction(I_INC, new CArgument(reg));
			    i->next = insn->next;
			    i->prev = insn;
			    insn->next->prev = i;
			    insn->next = i;
			    values[reg].set_const(insn, insn->args[1]);
			    return i;
			}
		    } else if(difference==-1 || difference==-2) {
			/* Änderung um -1 oder -2 durch DEC ausgleichen */
			insn->insn = I_DEC;
			changed = true;
			if(difference==-2) {
			    CInstruction* i = new CInstruction(I_DEC, new CArgument(reg));
			    i->next = insn->next;
			    i->prev = insn;
			    insn->next->prev = i;
			    insn->next = i;
			    values[reg].set_const(insn, insn->args[1]);
			    return i;
			}
		    }
		}
	    }
	    /* wir laden die Konstante `echt' */
	    values[insn->args[0]->reg].set_const(insn, insn->args[1]);
	    return insn;
	} else if(is_safe_mem(insn->args[1])) {
	    /*
	     * mov reg,[mem]
	     */
	    for(int i=rAX; i<=rDI; i++/*rDS; (i==rDI) ? (i=rES) : (i++)*/) {
		if(values[i].type==TValue::MEMORY && *values[i].value==*insn->args[1]) {
		    /* wir haben den Wert schon in einem Register */
		    /* ersetze mov r,m durch mov r,r */
		    delete insn->args[1];
		    insn->args[1] = new CArgument(TRegister(i));
                    values[insn->args[0]->reg] = values[insn->args[1]->reg];
		    changed = true;
		    return insn;
		}
	    }
            for(int i=rES; i<=rDS; i++) {
                if(values[i].type==TValue::SEGMENT) {
                    values[i].value->inc_imm(+2);
                    bool ok = *values[i].value == *insn->args[1];
                    values[i].value->inc_imm(-2);
                    if(ok) {
                        /* wir haben den Wert schon in einem Segment-Register */
                        /* ersetze mov r,m durch mov r,r */
                        delete insn->args[1];
                        insn->args[1] = new CArgument(TRegister(i));
                        changed = true;
                        return insn;
                    }
                }
            }
	    /* wir haben den Wert noch nicht */
	    values[insn->args[0]->reg].set_mem(insn, insn->args[1]);
	} else {
	    /*
	     * unbekannter mov (z.B. mov ax,[es:di])
	     */
	    use_mem(insn->args[1]);
	    set_unknown(insn->args[0]->reg, true);
	}
    } else if(insn->args[0]->is_byte_reg()) {
	/*
	 * mov rb,XXX
	 */
	use_mem(insn->args[1]);
	set_unknown(insn->args[0]->reg, true);
    } else if(insn->args[0]->is_seg_reg()) {
	/*
	 * mov Sr,XXX
	 */
	TRegister reg = insn->args[0]->reg;
	if(is_safe_mem(insn->args[1])) {
	    bool delete_it = false;
	    if(values[reg].type == TValue::MEMORY
	       && values[reg].value
	       && *values[reg].value == *insn->args[1]) {
		/* wir haben den Wert schon -> diesen Befehl löschen */
		delete_it = true;
	    } else if(values[reg].type == TValue::SEGMENT && values[reg].value) {
		/* wir haben den Wert mit einem LES geholt */
		CArgument* a = insn->args[1];
		a->inc_imm(-2);
		if(*values[reg].value == *a) delete_it = true;
		a->inc_imm(+2);
	    }
	    /* ok, wir können den Befehl löschen */
	    if(delete_it && insn->prev && insn->next) {
		CInstruction* i = insn->prev;
		insn->prev->next = insn->next;
		insn->next->prev = insn->prev;
		delete insn;
		changed = true;
		return i;
	    }
	    /* können wir optimieren? */
	}
	optimize_argument(2, insn->args[1], false);
	if(insn->args[1]->type == CArgument::MEMORY) {
	    int j=-1;
	    if(do_size) for(int i=rES; i<=rDS; i++) {
		if(values[i].type != TValue::UNKNOWN && values[i].value
		   && *values[i].value == *insn->args[1]) {
		    j=i;
		    break;
		}
	    }
	    if(j==-1 && insn->prev) {
		/* schade, haben wir nicht */
		values[reg].set_mem(insn, insn->args[1]);
		use_mem(insn->args[1]);
	    } else {
		/* schon in einem anderen Segmentregister */
		CInstruction* in = new CInstruction(I_PUSH, new CArgument(TRegister(j)));
		delete insn->args[1];
		insn->args[1] = 0;
		insn->insn = I_POP;
		in->prev = insn->prev;
		insn->prev = in;
		in->next = insn;
		in->prev->next = in;
		values[reg].set_mem(0, values[j].value);
	    }
	} else if(insn->args[1]->is_word_reg()) {
	    use_reg(insn->args[1]->reg);
	    if(values[insn->args[1]->reg].type == TValue::CONSTANT)
		set_unknown(insn->args[0]->reg, true);
	    else
		values[insn->args[0]->reg] = values[insn->args[1]->reg];
	}
    } else if(insn->args[1]->is_word_reg() || insn->args[1]->is_seg_reg()) {
	/*
	 * mov XXXX,reg
	 */
	use_mem(insn->args[0]);
	use_reg(insn->args[1]->reg);
	if(is_safe_mem(insn->args[0])) {
	    CValueIterator vi;
	    TValue* v;
	    while((v = vi.get_next())) {
		if(v->value && *v->value == *insn->args[0]) {
		    v->insn = 0;
		    v->clear();
		}
	    }
	    if(values[insn->args[1]->reg].type != TValue::CONSTANT)
		values[insn->args[1]->reg].set_mem(0, insn->args[0]);
	}
    } else if(insn->args[0]->type == CArgument::MEMORY) {
	use_mem(insn->args[0]);
        if(insn->opsize == 2 && insn->args[1]->type == CArgument::IMMEDIATE) {
            TRegister r1 = find_reg_with_value(insn->args[1]);
            if(r1 != rNONE) {
                use_reg(r1);
                delete insn->args[1];
                insn->args[1] = new CArgument(r1);
                changed = true;
                return insn;
            }
        }
	use_argument(insn->args[1]);
    } else
	set_unknown();
    return insn;
}

/* 
 *  LES-Anweisung
 */
CInstruction* optimize_les(CInstruction* insn)
{
    TRegister seg = ((insn->insn==I_LES)?rES:rDS);
    TRegister reg = insn->args[0]->reg;

    if(is_safe_mem(insn->args[1])) {
	if(values[reg].type == TValue::MEMORY
	   && *values[reg].value == *insn->args[1]) {
	    /* wir haben den Offset schon im Register */
	    if(values[seg].type == TValue::SEGMENT
	       && *values[seg].value == *insn->args[1]) {
		/* wir haben sogar das Segment schon */
		/* der LxS Befehl kann gelöscht werden */
		if(insn->next && insn->prev) {
		    CInstruction* i = insn->prev;
		    insn->next->prev = insn->prev;
		    insn->prev->next = insn->next;
		    delete insn;
		    changed = true;
		    return i;
		}
	    } else {
		/* nur das Segment laden */
		insn->insn = I_MOV;
		delete insn->args[0];
		insn->args[0] = new CArgument(seg);
		insn->args[1]->inc_imm(2);
		changed = true;
		if(insn->prev) insn=insn->prev;
		return insn;
	    }
	} else {
	    /* den Offset haben wir noch nicht */
	    if(values[seg].type == TValue::SEGMENT
	       && *values[seg].value == *insn->args[1]) {
		/* aber das Segment */
//                 cout << "optimierung: " << hex << insn->ip
//                      << "  values[seg].value.imm = " << values[seg].value->immediate
//                      << "  insn->args[1]->imm = " << insn->args[1]->immediate << dec << endl;
		insn->insn = I_MOV;
		changed = true;
		if(insn->prev) insn=insn->prev;
		return insn;
	    } else {
		/* wir haben noch nix */
		values[reg].set_mem(insn, insn->args[1]);
		values[seg].set_mem(insn, insn->args[1]);
		values[seg].type = TValue::SEGMENT;
		return insn;
	    }
	}
    } else {
	use_mem(insn->args[1]);
	set_unknown(seg, true);
	set_unknown(reg, true);
    }
    return insn;
}

/*
 *  PUSH-Insns
 */
void optimize_push(CInstruction* insn)
{
    if(insn->args[0]->is_word_reg()) {
	TRegister reg = insn->args[0]->reg;
	/*
	 *  mov reg,imm
	 *  push reg
	 */
	if(values[reg].type == TValue::CONSTANT && !values[reg].used) {
            /* heuristics for OOP:
                 mov di, obj
                 ...
                 push di
                 mov di, [di.vmt] */
            if(insn->next->args[1] && insn->next->args[1]->uses_reg(reg)) {
                use_reg(reg);
            } else {
                values[reg].known = true;
                delete insn->args[0];
                insn->args[0] = new CArgument(*(values[reg].value));
                changed = true;
            }
	} else
	    use_reg(reg);
    } else {
	optimize_argument(2, insn->args[0], true);
    }
    /* `fallthrough' */
    
    if(insn->args[0]->type==CArgument::IMMEDIATE) {
	TValue* v = new TValue;
	v->set_const(insn, insn->args[0]);
	push_value(v);
    } else if(insn->args[0]->type==CArgument::MEMORY) {
	TValue* v = new TValue;
	v->set_mem(insn, insn->args[0]);
	push_value(v);
    } else
	push_value();
}

/*
 *  POP-Insns
 */
void optimize_pop(CInstruction* insn)
{
    if(insn->args[0]->is_word_reg()) {
	TRegister reg = insn->args[0]->reg;
	TValue* v = pop_value();
	if(!v || v->type==TValue::UNKNOWN) {
	    set_unknown(reg, true);
	} else if((v->type==TValue::CONSTANT || v->type==TValue::MEMORY) && v->insn) {
	    /* eine Sequenz a la
	     *   push const
	     *   pop  reg
	     * wie sie bei INLINE() Prozeduren entsteht */
	    v->used = true;
	    insn->insn = I_MOV;
	    insn->args[1] = new CArgument(*(v->value));
	    insn->set_os(2);
	    v->kill_insn();
	    changed = true;
	    if(v->type==TValue::CONSTANT)
		values[reg].set_const(insn, insn->args[1]);
	    else
		values[reg].set_mem(insn, insn->args[1]);
	} else {
	    set_unknown(reg, true);
        }
	delete v;
    } else {
	delete pop_value();
        if(insn->args[0]->type == CArgument::REGISTER)
            set_unknown(insn->args[0]->reg, true);
        else
            use_mem(insn->args[0]);
    }
}

/*
 *  Funktionsende
 */
void optimize_ret(CInstruction* insn)
{
    if(insn->next) return;  // wir operieren nur mit dem Funktionsende
    values[rAX].used = true; // int
    values[rDX].used = true; // long
    values[rBX].used = true; // real

    insn = insn->prev;
    while(insn) {
	CInstruction* prev = insn->prev;
	switch(insn->insn) {
         case I_POP:
	    /* pop bp -- Stackrahmen-Abbau */
	    if(insn->args[0]->type != CArgument::REGISTER ||
	       insn->args[0]->reg != rBP) return;
	    break;
         case I_MOV:
	    if(insn->args[0]->type == CArgument::REGISTER) {
		/* mov sp,bp */
		if(insn->args[0]->reg != rSP) return;
	    } else if(insn->args[0]->type == CArgument::MEMORY) {
		if(insn->args[0]->segment != rSS
		   || !is_safe_mem(insn->args[0])) return;
		/* mov [ss:XXX],irgendwas
		 * Killen, da niemand mehr ins SS sehen will
		 * außer evtl. bei nested procedures, aber da
		 * wird nicht über BP adressiert
		 * damit wird die Kette
		 *   mov ax,[ss:bp+XX]    ; Func := a;
		 *   mov [ss:bp+YY],ax
		 *   mov ax,[ss:bp+YY]    ; END;
		 * am Ende vieler Funktionen reduziert */
		if(!insn->prev || !insn->next) return;
		insn->prev->next = insn->next;
		insn->next->prev = insn->prev;
		delete insn;
		changed = true;
	    } else return;
         case I_LEAVE:
	    break;
         default:
	    return;
	}
	insn = prev;
    }
}

/*
 *  Optimiert bedingte Sprünge / SETcc
 */
CInstruction* optimize_jcc_or_setcc(CInstruction* insn)
{
    int cc = insn->param;
    CInstruction* ret = insn;
    /* Ein bedingter Sprung steht mit ziemlicher Sicherheit nach
     * einem Befehl, der die Flags änderte. In diesem Fall darf
     * der Zieloperand des Befehls nicht als nutzlose Berechnung
     * verworfen werden. */
    int max_insn = 10; /* max 10 Befehle zurückschauen */
    while(insn->prev && max_insn-->0) {
	insn = insn->prev;
	switch(insn->insn) {
         case I_JCC:
	    /* die Behandlung wurde bereits durchgeführt */
	    /* JCXZ kann hier ignoriert werden */
         case I_LABEL:
         case I_CALLN:
         case I_CALLF:
         case I_JMPN:
         case I_JMPF:
	    /* diese löschen eh alle Flags */
	    goto exit_loop;
	    
         case I_INC:
         case I_DEC:
	    if(cc==2 || cc==3) break; /* INC/DEC ändern das CF nicht.
				       * Programmierer wissen das. */
	    /* FALLTHROUGH */
         case I_ADD:
         case I_OR:
         case I_ADC:
         case I_SBB:
         case I_AND:
         case I_SUB:
         case I_XOR:
         case I_CMP:
         case I_NOT:
         case I_NEG:
//	case I_TEST:
	    /* das ist eine Arithmetik-Insn, die diesen Sprung beeinflußt */
	    /* Suche alle Werte, die darauf verweisen */
	    {
		CValueIterator vi;
		TValue* p;
		while((p=vi.get_next())) {
		    if(p->insn == insn)
			p->insn = 0;
		}
		break;
	    }
         case I_IMUL:
         case I_IDIV:
         case I_MUL:
         case I_DIV:
	    /* FIXME: diese können auch als Sprung-Trigger benutzt werden. */
	    /* das ist jedoch ungebräuchlich */
	    break;
         case I_STRING:
            /* FIXME: cmpsb? */
            break;
	default:
	    break;
	}
    }
 exit_loop:

    /* Spruenge benoetigen potentiell alle Register am "anderen" Ende */
    if(ret->insn == I_JCC)
        for(int r = rAX; r < rMAX; r++)
            values[r].used = true;

    if(ret->insn == I_JCC && ret->param == 5 && ret->prev) {
        /* jnz garantiert, daß das Ergebnis des vorigen Befehls 0 war */
        insn = ret->prev;
        if(insn->insn == I_OR || insn->insn == I_AND || insn->insn == I_XOR
           || insn->insn == I_ADD || insn->insn == I_SUB || insn->insn == I_ADC
           || insn->insn == I_SBB || insn->insn == I_INC || insn->insn == I_DEC) {
            if(insn->args[0]->type == CArgument::REGISTER && insn->args[0]->is_word_reg()) {
                if(!ret->args[1])
                    ret->args[1] = new CArgument(int(0));
                TRegister r = insn->args[0]->reg;
                values[r].used = true;
                values[r].set_const(0, ret->args[1]);
            }                
        }
    }

    if(ret->insn == I_SETCC) {
	destroy_argument(ret->args[0], false);
    }
    return ret;
}

/*
 *  Vergleiche
 */
void optimize_cmp(CInstruction* insn)
{
    optimize_argument(insn->opsize, insn->args[0], false);
    optimize_argument(insn->opsize, insn->args[1], true);

//     if(insn->args[0]->type == CArgument::REGISTER
//        && insn->args[1]->type != CArgument::IMMEDIATE
//        && values[insn->args[0]->reg].type == TValue::CONSTANT) {
// 	/* cmp const,[foo] */
// 	/* umtauschen, Sprünge anpassen */
// 	static signed char jump_tab[16] =
// 	  { -1, -1, 7, 6, 4 ,5, 3, 2, -1, -1, -1, -1, 15, 14, 13, 12 };
// 	CInstruction* q = insn->next;
// 	while(q && q->insn==I_JCC) {
// 	    if(jump_tab[q->param]==-1) break;
// 	    q = q->next;
// 	}

// 	if(!q || q->insn!=I_JCC) {
// 	    /* alle Sprünge können angepaßt werden */
// 	    CArgument* a = insn->args[0];
// 	    insn->args[0] = insn->args[1];
// 	    insn->args[1] = a;

// 	    q = insn->next;
// 	    while(q && q->insn==I_JCC) {
// 		q->param = jump_tab[q->param];
// 		q = q->next;
// 	    }
// 	    changed = true;
// 	}
//     }
}

/*
 *  true, wenn Register r 0 ist
 */
bool reg_zero(TRegister reg)
{
    return values[reg].type == TValue::CONSTANT
        && values[reg].value->reloc==0 && values[reg].value->immediate==0;
}

/*
 *  Arithmetik
 */
CInstruction* optimize_arit(CInstruction* insn)
{
    if(insn->args[0]->is_word_reg() && insn->args[1]->is_word_reg()) {
	TRegister reg = insn->args[0]->reg;
        if(insn->args[0]->reg == insn->args[1]->reg
           && (insn->insn == I_XOR || insn->insn == I_SUB)) {
            /*
             * xor reg,reg
             * sub reg,reg
             */
            if(values[reg].type==TValue::CONSTANT
               && values[reg].value->reloc==0 && values[reg].value->immediate==0) {
                /* die Null haben wir schon im Register */
                /* xor wegwerfen */
                if(insn->prev && insn->next) {
                    CInstruction* i;
                    insn->next->prev = insn->prev;
                    insn->prev->next = insn->next;
                    i = insn->prev;
                    delete insn;
                    changed = true;
                    return i;
                }
            }
            delete insn->args[2];
            insn->args[2] = new CArgument(0);
            values[reg].set_const(insn, insn->args[2]);
            return insn;
        }

        /* ist es ein Befehl mit einem Nullargument? */
        if(reg_zero(insn->args[1]->reg)) {
            /*
             *  op reg, 0
             *  hat dieselbe Wirkung wie `or reg, reg'
             *  ... welchen der Optimizer kickt, falls nicht
             *  benötigt
             */
            if(insn->insn != I_ADC && insn->insn != I_SBB && insn->insn != I_ADD) {
                /* add, sub, or, xor, cmp */
                delete insn->args[1];
                insn->args[1] = new CArgument(*insn->args[0]);
                changed = true;
                return insn;
            }
        } else if(reg_zero(insn->args[0]->reg)) {
            /*
             *  op 0, reg
             */
            switch(insn->insn) {
             case I_ADD:
             case I_OR:
             case I_XOR:
                /* dasselbe wie MOV */
                insn->insn = I_MOV;
                changed = true;
                break;
             case I_AND:
                /* fast-nop */
                if(insn->prev && insn->next && flag_check(insn->next)) {
                    CInstruction* i;
                    insn->next->prev = insn->prev;
                    insn->prev->next = insn->next;
                    i = insn->prev;
                    delete insn;
                    changed = true;
                    return i;
                }
                break;
             default:;
            } /* kein optimierbarer Befehl */
        } /* 2. Argument != 0 */
    }
    
    optimize_argument(insn->opsize, insn->args[1], true);
    destroy_argument(insn->args[0], true);

    return insn;
}

/*
 *  Shift
 */
void optimize_shift(CInstruction* insn)
{
    use_argument(insn->args[1]);
    destroy_argument(insn->args[0], true);
}

/*
 *  Drei-Argumente-IMUL
 */
void optimize_imul3(CInstruction* insn)
{
    optimize_argument(insn->opsize, insn->args[1], false);
    destroy_argument(insn->args[0], false);
}

/*
 *  LEA
 */
void optimize_lea(CInstruction* insn)
{
    if(insn->args[1]->type != CArgument::MEMORY
       || insn->args[0]->type != CArgument::REGISTER) {
	set_unknown();
	return;
    }
    if(insn->args[1]->memory[0]==rNONE && insn->args[1]->memory[1]==rNONE) {
	/* LEA reg,[disp16] */
	insn->insn = I_MOV;
	insn->args[1]->type = CArgument::IMMEDIATE;
	values[insn->args[0]->reg].set_const(insn, insn->args[1]);
	changed = true;
	return;
    }
    if((insn->args[1]->memory[0]==rNONE || insn->args[1]->memory[1]==rNONE)
       && insn->args[1]->reloc==0 && insn->args[1]->immediate==0) {
	/* LEA reg,[reg] */
	TRegister reg = insn->args[1]->memory[0];
	if(reg==rNONE) reg = insn->args[1]->memory[1];

	insn->insn = I_MOV;
	delete insn->args[1];
	insn->args[1] = new CArgument(reg);
	if(reg != insn->args[0]->reg)
	    values[reg] = values[insn->args[0]->reg];
	changed = true;
	return;
    }

    /* unoptimierbares LEA */
    use_mem(insn->args[1]);
    destroy_argument(insn->args[0], false);
}

/*
 *  XCHG
 */
void optimize_xchg(CInstruction* insn)
{
    if(insn->args[0]->type == CArgument::REGISTER
       && insn->args[1]->type == CArgument::REGISTER) {
	/* XCHG reg,reg */
	/* tausche die Werte aus */
	values[insn->args[0]->reg].swap(values[insn->args[1]->reg]);
    } else {
	destroy_argument(insn->args[0], true);
	destroy_argument(insn->args[1], true);
    }
}

/*
 *  Division
 */
void optimize_div(CInstruction* insn)
{
    optimize_argument(insn->opsize, insn->args[0], false);
    use_reg(rAX);
    set_unknown(rAX, true);
    if(insn->opsize==2) {
	use_reg(rDX);
	set_unknown(rDX, true);
    }
}

/*
 *  Multiplikation
 */
void optimize_mul(CInstruction* insn)
{
    if(insn->insn==I_IMUL && insn->args[0]->is_word_reg()
       && values[insn->args[0]->reg].type==TValue::CONSTANT
       && values[insn->args[0]->reg].value->is_immed(1)) {
        /* imul X, where X is 1 */
        values[insn->args[0]->reg].known = true;
        insn->insn = I_CWD;
        delete insn->args[0];
        insn->args[0] = 0;
        set_unknown(rDX, true);
        changed = true;
        return;
    }
    optimize_argument(insn->opsize, insn->args[0], false);
    if(insn->opsize==2) {
	use_reg(rAX);
	set_unknown(rAX);
	set_unknown(rDX);
    } else {
	use_reg(rAL);
	set_unknown(rAX);
    }
}

/*
 *  Unäre Operationen (inc, dec, not, neg)
 */
void optimize_unary(CInstruction* insn)
{
    if(insn->args[0]->is_word_reg()
       && values[insn->args[0]->reg].type==TValue::CONSTANT) {
	/* OP reg
	 * wobei reg einen bekannten Wert hat
	 * -> latürnich kennen wir auch den Wert danach */
	TRegister reg = insn->args[0]->reg;
	use_reg(reg);
	delete insn->args[1];
	insn->args[1]=0;
	switch(insn->insn) {
         case I_INC:
	    insn->args[1] = new CArgument(*values[reg].value);
	    insn->args[1]->inc_imm(1);
            values[reg].set_const(insn, insn->args[1]);
	    return;
         case I_DEC:
	    insn->args[1] = new CArgument(*values[reg].value);
	    insn->args[1]->inc_imm(-1);
            values[reg].set_const(insn, insn->args[1]);
	    return;
         case I_NOT:
	    if(values[reg].value->reloc == 0) {
		insn->args[1] = new CArgument(~values[reg].value->immediate);
		return;
	    }
	    break;
         case I_NEG:
	    if(values[reg].value->reloc == 0) {
		insn->args[1] = new CArgument(-values[reg].value->immediate);
		return;
	    }
	    break;
         default:
	    break;
	}
    }
    destroy_argument(insn->args[0], true);
}

/*
 * Hauptroutine
 */
void data_flow_analysis(CInstruction* insn)
{
    stack = 0;
    set_unknown();
    make_backlinks(insn);

    while(insn) {
	cur_ip = insn->ip;
        
	switch(insn->insn) {
         case I_LABEL:
         case I_JMPN:
         case I_JMPF:
         case I_CALLN:
         case I_CALLF:
	    set_unknown();
	    break;
         case I_JCXZ:
	    use_reg(rCX);
	    break;
         case I_JCC:
         case I_SETCC:
	    insn = optimize_jcc_or_setcc(insn);
	    break;
         case I_MOV:
	    insn = optimize_mov(insn);
	    break;
         case I_PUSH:
	    optimize_push(insn);
	    break;
         case I_POP:
	    optimize_pop(insn);
	    break;
         case I_CMP:
	    optimize_cmp(insn);
	    break;
         case I_LES:
         case I_LDS:
	    insn = optimize_les(insn);
	    break;
         case I_ADD:
         case I_ADC:
         case I_SUB:
         case I_SBB:
         case I_OR:
         case I_AND:
         case I_XOR:
	    insn = optimize_arit(insn);
	    break;
         case I_INC:
         case I_DEC:
         case I_NOT:
         case I_NEG:
	    optimize_unary(insn);
	    break;
         case I_IMUL:
	    if(insn->args[2])
		optimize_imul3(insn);
	    else
		optimize_mul(insn);
	    break;
         case I_MUL:
	    optimize_mul(insn);
	    break;
         case I_XCHG:
	    optimize_xchg(insn);
	    break;
         case I_SHL:
         case I_SHR:
         case I_SAR:
         case I_RCL:
         case I_RCR:
         case I_ROL:
         case I_ROR:
	    optimize_shift(insn);
	    break;
         case I_CWD:
	    use_reg(rAX);
	    set_unknown(rDX, true);
	    break;
         case I_CBW:
	    use_reg(rAL);
	    set_unknown(rAH, true);
	    break;
         case I_DIV:
         case I_IDIV:
	    optimize_div(insn);
	    break;
         case I_RETF:
         case I_RETN:
	    optimize_ret(insn);
	    break;
         case I_LEA:
	    optimize_lea(insn);
	    break;
         case I_FLAG:
            break;
         case I_STRING:
            // FIXME: do something sensible.
            use_reg(rSI);
            use_reg(rDI);
            use_reg(rES);
            use_reg(rCX);
            use_reg(rAX);
            set_unknown();
         default:
	    set_unknown();
	}
	insn = insn->next;
    }
}

bool can_modify_reg(CInstruction* p, TRegister r)
{
    while(p) {
        switch(p->insn) {
         case I_IMUL:
            if(!p->args[2]) {
                /* ein-Operand-Form wird nicht unterstützt */
                return false;
            }
            /* FALLTHROUGH */
         case I_MOV:
         case I_LEA:
         case I_LES:
         case I_LDS:
            if(p->args[1]->uses_reg_part(r))
                return false;
            if(p->args[0]->is_reg(r))
                return true;
            break;
         case I_JMPN:
            if(p->args[0]->type == CArgument::LABEL)
                p = p->args[0]->label;
            else
                return false;
            break;
         case I_JCC:
            if(!can_modify_reg(p->args[0]->label->next, r))
                return false;
            break;
         case I_LABEL:
            break;
         default:
            return false;
        }
        p = p->next;
    }
    return true;
}

