/*
 *  Verwaltung von Assembler-Befehlen für tputuner
 *
 *  (c) copyright 1998,1999,2000 by Stefan Reuther
 *
 *  Jeder Befehl ist ein CInstruction-Objekt, die Argumente sind
 *  CArgument'e. Es gibt keine Subklassen für spezielle Befehle
 *  -> Musterbeispiel für schlechtes OO ;-)
 */
#include "insn.h"
#include <iostream>
#include <cstring>
#include <string>
#include "codewriter.h"
#include "assemble.h"
#include "global.h"
#include "tpufmt.h"

TRegister bases[8] = { rBX, rBX, rBP, rBP, rSI, rDI, rBP, rBX };
TRegister index[8] = { rSI, rDI, rSI, rDI, rNONE, rNONE, rNONE, rNONE };
TRegister def_seg[8] = { rDS, rDS, rSS, rSS, rDS, rDS, rSS, rDS };

char reg_sizes[] = { 0,
		     2, 2, 2, 2, 2, 2, 2, 2,
		     1, 1, 1, 1, 1, 1, 1, 1,
		     2, 2, 2, 2,
                     0 };

char reg_values[] = { 0,
		      0, 1, 2, 3, 4, 5, 6, 7,
		      0, 1, 2, 3, 4, 5, 6, 7,
		      0, 1, 2, 3,
                      0 };

char* insn_names[] = {
    "INVALID",
    "label",
    "mov", "les", "lds", "xchg", "lea",

    "pop", "push",

    "dec", "inc",
    "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp",
    "imul", "mul", "div", "idiv",
    "not", "neg",

    "jcc", "callf", "calln", "jmpn", "jmpf", "retn", "retf",
    "jcxz",
	       
    "cbw", "cwd",

    "rol", "ror", "rcl", "rcr", "shl", "shr", "sar",
	       
    "leave", "enter",

    "flag", "string",

    "setcc"
};

/* Anzahl signifikante Operanden pro Befehl */
char insn_argc[] = {
    0,                          // invalid
    0,                          // label
    2,2,2,2,2,                  // daten-transfer
    1,1,                        // Stack
    1,1,                        // inc/dec
    2,2,2,2,2,2,2,2,            // arit
    3,1,1,1,                    // unary (imul kann 3 Operanden haben)
    1,1,                        // unary
    1,1,1,1,1,1,1,1,            // programm-transfer
    0,0,                        // cbw/cwd
    2,2,2,2,2,2,2,              // shift
    0,2,                        // enter/leave
    1                           // setcc
};

char* reg_names[] = {
    "NONE",
    "ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
    "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh",
    "es", "cs", "ss", "ds"
};

/*
 *  Relozierungseintrag
 */
CRelo::CRelo(char* p)
{
    unitnum = *p++;
    rtype = *p++;
    rblock = (unsigned char)*p++;
    rblock += 256 * (unsigned char)*p++;
    rofs = (unsigned char)*p++;
    rofs += 256 * (unsigned char)*p++;
}

void CRelo::print(ostream& cout)
{
    cout << "<un=" << (unsigned int)unitnum << ",rt=" << (unsigned int)rtype
	 << ",rb=" << rblock << ",ro=" << rofs << ">";
}

bool CRelo::operator==(const CRelo& c) const
{
    return unitnum==c.unitnum && rtype==c.rtype && rblock==c.rblock && rofs==c.rofs;
}

/*
 *  Ein Parameter einer Instruction
 */
CArgument::CArgument(TRegister areg)
    : type(REGISTER), reg(areg), reloc(0), label(0)
{}

CArgument::CArgument(TRegister base, TRegister index, int immed, CRelo* areloc,
		     TRegister seg)
    : type(MEMORY), immediate(immed), reloc(areloc), segment(seg), label(0)
{
    memory[0] = base;
    memory[1] = index;
}

CArgument::CArgument(int immed, CRelo* areloc)
    : type(IMMEDIATE), immediate(immed), reloc(areloc), label(0)
{}

CArgument::CArgument(CInstruction* l)
    : type(LABEL), reloc(0), label(l)
{}

CArgument::CArgument(const CArgument& arg)
    : type(arg.type), reg(arg.reg), immediate(arg.immediate),
      reloc(0), segment(arg.segment), label(arg.label)
{
    memory[0] = arg.memory[0];
    memory[1] = arg.memory[1];
    if(arg.reloc) reloc = new CRelo(*(arg.reloc));
    if(label) label->inc_ref();
}

CArgument::~CArgument()
{
    if(reloc) delete reloc;
    if(label) label->dec_ref();
}

// true, wenn der Operand das Register /r/ benutzt
bool CArgument::uses_reg(TRegister r)
{
    if(type==REGISTER)
	return reg==r;
    else if(type==MEMORY)
	return (r==memory[0] || r==memory[1]);
    else
	return false;
}

// true, wenn der Operand Register /r/ enthält
// (der Befehl ändert den Operanden. Beeinflußt das r?)
bool CArgument::affects_reg(TRegister r)
{
    if(type==REGISTER)
	return alias_reg(reg, r);
    else
        return false;
}

// true, wenn der Operand Register /r/ benutzt (evtl auch Teile davon)
bool CArgument::uses_reg_part(TRegister r)
{
    if(type==REGISTER)
	return alias_reg(reg, r);
    else if(type==MEMORY)
	return alias_reg(memory[0], r) || alias_reg(memory[1], r);
    else
	return false;
}

// liefert Operandengröße
int CArgument::get_size()
{
    if(type==REGISTER) {
	return reg_sizes[reg];
    }
    return 0;
}

bool CArgument::operator==(const CArgument& c) const
{
    if(type != c.type) return false;
    switch(type) {
    case REGISTER:
	return reg == c.reg;
    case MEMORY:
    case IMMEDIATE:
	if(reloc) {
	    if(!c.reloc || *(c.reloc) != *reloc) return false;
	} else {
	    if(c.reloc || immediate!=c.immediate) return false;
	}
	if(type==IMMEDIATE) return true;
	return segment==c.segment && ((memory[0]==c.memory[0] && memory[1]==c.memory[1])
				      || (memory[0]==c.memory[1] && memory[1]==c.memory[0]));
    case LABEL:
	return label->ip==c.label->ip;
    }
    return false;
}

// imm16 Argument ausgeben (disp16 / imm16, kein rel16)
void CArgument::write_immed(CCodeWriter& w)
{
    if(reloc) {
	w.put_reloc(reloc);
    }
    w.write_word(immediate);
}

// Segmentpräfix ausgeben
static void seg_override(CCodeWriter& w, TRegister seg)
{
    switch(seg) {
    case rDS: w.wb(0x3E); break;
    case rES: w.wb(0x26); break;
    case rCS: w.wb(0x2E); break;
    case rSS: w.wb(0x36); break;
    default: break;
    }
}

// modr/m ausgeben, wobei dieser Operand das r/m ist
void CArgument::write_rm(CCodeWriter& w,   // wohin ausgeben?
			 int opc,          // Opcode
			 int areg,         // reg-Feld für modr/m
			 bool with_seg,    // true->Segmentpräfixe ausgeben
			 int prefix)       // !=0 ->Opcodepräfix (0x0F)
{
    areg = (areg & 7) << 3;

    /* ich bin ein Register */
    if(type==REGISTER) {
	if(prefix) w.wb(prefix);
	w.wb(opc);
	w.wb(areg | reg_values[reg] | 0xC0);
	return;
    }

    if(type!=MEMORY) throw string("Instruction needs memory operand");
    
    /* ich bin ein Speicheroperand */

    /* [disp16] */
    if(memory[0]==rNONE && memory[1]==rNONE) {
	if(segment != rDS && with_seg) seg_override(w, segment);
	if(prefix) w.wb(prefix);
	w.wb(opc);
	w.wb(areg | 6);
	if(reloc)
	    w.put_reloc(reloc);
	w.write_word(immediate);
	return;
    }

    /* suche Registerkombination */
    int lo = -1;
    for(int i=0; i<8; i++)
	if((memory[0]==bases[i] && memory[1]==index[i])
	   || (memory[1]==bases[i] && memory[0]==index[i])) {
	    lo = i;
	    break;
	}
    if(lo == -1)
	throw string("Impossible index/base combination");

    if(segment != def_seg[lo] && with_seg) seg_override(w, segment);
    if(prefix) w.wb(prefix);
    w.wb(opc);
    areg |= lo;

    if(reloc || immediate<-128 || immediate>127) {
	/* disp16 oder Relokation */
	w.wb(areg | 0x80);
	if(reloc)
	    w.put_reloc(reloc);
	w.write_word(immediate);
    } else if(immediate==0 && lo!=6) {
	/* kein disp */
	w.wb(areg);
    } else {
	/* disp8 */
	w.wb(areg | 0x40);
	w.wb(immediate);
    }
}

// Differenz der Adressen zweier Argumente (`this - other')
// liefert 0 wenn nicht vergleichbar (!)
// (dann ist aber *this == *other)
int CArgument::adr_diff(CArgument* other)
{
    if(type != MEMORY || other->type != MEMORY)
        return 0;

    // FIXME -- [bx+di+N] ist nicht vergleichbar mit [di+bx+N]
    // passiert das aber?
    if(segment != other->segment || memory[0] != other->memory[0]
       || memory[1] != other->memory[1])
        return 0;

    if(reloc) {
        if(!other->reloc)
            return 0;
        CRelo* r = other->reloc;
        if(reloc->unitnum != r->unitnum || reloc->rtype != r->rtype
           || reloc->rblock != r->rblock)
            return 0;
        return reloc->rofs - r->rofs;
    } else {
        if(other->reloc)
            return 0;
        return immediate - other->immediate;
    }
}

// in menschenlesbarer Form ausgeben
void CArgument::print(ostream& cout)
{
    switch(type) {
    case REGISTER:
	cout << reg_names[reg];
	break;
    case MEMORY:
	cout << "[" << reg_names[segment] << ":";
	for(int i=0; i<2; i++) if(memory[i]!=rNONE) cout << reg_names[memory[i]] << "+";
	if(reloc) reloc->print(cout);
	else cout << immediate;
	cout << "]";
	break;
    case IMMEDIATE:
	if(reloc) reloc->print(cout);
	else cout << immediate;
	break;
    case LABEL:
	cout << "<label:" << hex << label->ip << ">";
	break;
    }
}

/*
 *  Maschinencode-Befehl
 */
CInstruction::CInstruction(TInsn which, CArgument* a1, CArgument* a2, CArgument* a3)
    : insn(which), next(0), prev(0), param(0), ip(0), temp(0)
{
    args[0] = a1;
    args[1] = a2;
    args[2] = a3;

    int n = 0;
    for(int i=0; i<3; i++) if(args[i]) n |= args[i]->get_size();
    if(n==1 || n==2) opsize = n;
    else opsize = 0;
}

CInstruction::~CInstruction()
{
    for(int i=0; i<3; i++)
	delete args[i];
}

struct CodeName {
    unsigned char code;
    const char* name;
};
static CodeName code_names[] = {
    { 0, "cc = 'o'" },
    { 1, "cc = 'no'" },
    { 2, "cc = 'b', 'c'" },
    { 3, "cc = 'nb', 'nc'" },
    { 4, "cc = 'e', 'z'" },
    { 5, "cc = 'ne', 'nz'" },
    { 6, "cc = 'na'" },
    { 7, "cc = 'a'" },
    { 8, "cc = 's', 'neg'" },
    { 9, "cc = 'ns', 'pos'" },
    { 10, "cc = 'pe'" },
    { 11, "cc = 'po'" },
    { 12, "cc = 'l'" },
    { 13, "cc = 'nl'" },
    { 14, "cc = 'ng'" },
    { 15, "cc = 'g'" },
    { CODE_CLD, "cld" },
    { CODE_STD, "std" },
    { CODE_REP, "rep" },
    { CODE_REPNE, "repne" },
    { CODE_MOVSB, "movsb" },
    { CODE_MOVSW, "movsw" },
    { CODE_CMPSB, "cmpsb" },
    { CODE_CMPSW, "cmpsw" },
    { CODE_LODSB, "lodsb" },
    { CODE_LODSW, "lodsw" },
    { CODE_SCASB, "scasb" },
    { CODE_SCASW, "scasw" },
    { CODE_STOSB, "stosb" },
    { CODE_STOSW, "stosw" },
    { 0, 0 }
};

// in menschenlesbarer Form ausgeben
void CInstruction::print(ostream& cout)
{
    int x = strlen(insn_names[insn]);
    cout << hex << ip << ":  " << insn_names[insn];
    if(opsize) cout << "(" << opsize << ")", x+=3;
    if(insn==I_JCC || insn==I_LABEL || insn==I_SETCC || insn==I_FLAG || insn==I_STRING)
	cout << "`" << param << "'", x+=3;
    while(x < 10) cout << " ", x++;

    for(int i=0; i<3; i++) if(args[i]) {
	if(i!=0) cout << ", ";
	args[i]->print(cout);
    }
    if(insn==I_LABEL) cout << "<" << ip << ">";
    if (insn==I_FLAG || insn==I_STRING || insn==I_SETCC || insn==I_JCC) {
        CodeName* c = code_names;
        while (c->name && c->code != param)
            ++c;
        if (c->name)
            cout << "    // " << c->name;
    }
    cout << dec << endl;
}

bool CInstruction::operator==(const CInstruction& c) const
{
    if(insn != c.insn) return false;
    if(opsize && c.opsize && opsize!=c.opsize) return false;
    if((insn==I_JCC || insn==I_SETCC) && param!=c.param) return false;
    if(insn==I_LABEL && ip!=c.ip) return false;
    for(int i=0; i<insn_argc[insn]; i++) {
	if(args[i]) {
	    if(!c.args[i] || *(c.args[i]) != *(args[i])) return false;
	} else {
	    if(c.args[i]) return false;
	}
    }
    return true;
}

/*
 * true, wenn ich mit Bytes arbeite, false für Words,
 * Fehler, wenn unbekannt (->Fehler des Disassemblers)
 */
bool CInstruction::byte_insn()
{
    if(opsize==1) return true;
    else if(opsize==2) return false;
    else throw string("Instruction without required operand size definition: ") + insn_names[insn];
}

/*
 *  diesen Befehl assemblieren
 */
CInstruction* CInstruction::assemble(CCodeWriter& w)
{
    switch(insn) {
    case I_LABEL: break;
    case I_INVALID:
	throw string("Invalid insn");
    case I_ADD:
	assemble_arit(w, this, 0);
	break;
    case I_OR:
	assemble_arit(w, this, 1);
	break;
    case I_ADC:
	assemble_arit(w, this, 2);
	break;
    case I_SBB:
	assemble_arit(w, this, 3);
	break;
    case I_AND:
	assemble_arit(w, this, 4);
	break;
    case I_SUB:
	assemble_arit(w, this, 5);
	break;
    case I_XOR:
	assemble_arit(w, this, 6);
	break;
    case I_CMP:
	assemble_arit(w, this, 7);
	break;
    case I_CBW:
	w.wb(0x98);
	break;
    case I_CWD:
	w.wb(0x99);
	break;
    case I_LEAVE:
	w.wb(0xC9);
	break;
    case I_ENTER:
	w.wb(0xC8);
	if(args[0]->type != CArgument::IMMEDIATE
	   || args[1]->type != CArgument::IMMEDIATE) throw string("Can't encode ENTER with non-const args");
	args[0]->write_immed(w);
	w.wb(args[1]->immediate);
	break;
    case I_PUSH:
	if(args[0]->is_word_reg()) {
	    /* push r16 */
	    w.wb(0x50 + reg_values[args[0]->reg]);
	} else if(args[0]->type==CArgument::IMMEDIATE) {
	    /* push imm */
	    if(args[0]->is_byte_imm()) {
                if(do_386 && next && next->insn==I_PUSH) {
                    if(next->args[0]->is_immed() && next->args[0]->is_byte_imm() &&
                       ((next->args[0]->immediate>=0 && args[0]->is_immed(0))
                        || (next->args[0]->immediate<0 && args[0]->is_immed(-1)))) {
                        //cout << "{0}";
                        w.wb(0x66);
                        w.wb(0x6A);
                        w.wb(next->args[0]->immediate);
                        next->ip = ip;
                        return next->next;
                    }
                }
		w.wb(0x6A);
		w.wb(args[0]->immediate);
	    } else {
                if(do_386 && next && next->insn==I_PUSH
                   && next->args[0]->type==CArgument::IMMEDIATE && !next->args[0]->is_byte_imm()) {
                    //cout << "{1}";
                    w.wb(0x66);
                    w.wb(0x68);
                    next->args[0]->write_immed(w);
                    args[0]->write_immed(w);
                    next->ip = ip;
                    return next->next;
                }
		w.wb(0x68);
		args[0]->write_immed(w);
	    }
	} else if(args[0]->is_seg_reg()) {
	    /* push Sr */
	    switch(args[0]->reg) {
	    case rES: w.wb(0x06); break;
	    case rCS: w.wb(0x0E); break;
	    case rSS: w.wb(0x16); break;
	    case rDS: w.wb(0x1E); break;
	    default: break;
	    }
	} else {
	    /* push rm16 */
            if(do_386 && next && next->insn==I_PUSH && next->args[0]->type==CArgument::MEMORY) {
                args[0]->inc_imm(-2);
                if(*args[0] == *next->args[0]) {
                    /* zwei push r/m zusammenfassen */
                    //cout << "{2}";
                    args[0]->write_rm(w, 0xFF, 6, true, 0x66);
                    args[0]->inc_imm(2);
                    next->ip = ip;
                    return next->next;
                }
                args[0]->inc_imm(2);
            }
	    args[0]->write_rm(w, 0xFF, 6);
        }
	break;
    case I_POP:
	if(args[0]->is_word_reg()) {
	    /* pop r16 */
	    w.wb(0x58 + reg_values[args[0]->reg]);
	} else if(args[0]->is_seg_reg()) {
	    /* pop Sr */
	    switch(args[0]->reg) {
	    case rES: w.wb(0x07); break;
	    case rCS: w.wb(0x0F); break;
	    case rSS: w.wb(0x17); break;
	    case rDS: w.wb(0x1F); break;
	    default: break;
	    }
	} else
	    /* pop rm16 */
	    args[0]->write_rm(w, 0x8F, 0);
	break;
    case I_INC:
	if(args[0]->is_word_reg()) {
	    /* inc r16 */
	    w.wb(0x40 + reg_values[args[0]->reg]);
	} else if(byte_insn()) {
	    /* inc rm8 */
	    args[0]->write_rm(w, 0xFE, 0);
	} else {
	    /* inc rm16 */
	    args[0]->write_rm(w, 0xFF, 0);
	}
	break;
    case I_DEC:
	if(args[0]->is_word_reg()) {
	    /* dec r16 */
	    w.wb(0x48 + reg_values[args[0]->reg]);
	} else if(byte_insn()) {
	    /* dec rm8 */
	    args[0]->write_rm(w, 0xFE, 1);
	} else {
	    /* dec rm16 */
	    args[0]->write_rm(w, 0xFF, 1);
	}
	break;
    case I_NOT:
	args[0]->write_rm(w, byte_insn()?0xF6:0xF7, 2);
	break;
    case I_NEG:
	args[0]->write_rm(w, byte_insn()?0xF6:0xF7, 3);
	break;
    case I_MUL:
	args[0]->write_rm(w, byte_insn()?0xF6:0xF7, 4);
	break;
    case I_DIV:
	args[0]->write_rm(w, byte_insn()?0xF6:0xF7, 6);
	break;
    case I_IDIV:
	args[0]->write_rm(w, byte_insn()?0xF6:0xF7, 7);
	break;
    case I_IMUL:
	if(args[1] && args[2]) {
	    if(!args[0]->is_word_reg()
	       || args[2]->type!=CArgument::IMMEDIATE) throw string("Invalid form of IMUL/3");
	    if(args[2]->is_byte_imm()) {
		/* imul r16,rm16,ib */
		args[1]->write_rm(w, 0x6B, reg_values[args[0]->reg]);
		w.wb(args[2]->immediate);
	    } else {
		/* imul r16,rm16,iw */
		args[1]->write_rm(w, 0x69, reg_values[args[0]->reg]);
		args[2]->write_immed(w);
	    }
	} else
	    /* imul rm */
	    args[0]->write_rm(w, byte_insn()?0xF6:0xF7, 5);
	break;
    case I_LES:
    case I_LDS:
    case I_LEA:
	if(!args[0]->is_word_reg() || args[1]->type!=CArgument::MEMORY)
	    throw string("Invalid form of LES/LDS/LEA");
	/* lXX r16,m */
	args[1]->write_rm(w,
			  (insn==I_LES)?0xC4:
			  (insn==I_LDS)?0xC5:0x8D,
			  reg_values[args[0]->reg],
			  insn != I_LEA);
	break;
    case I_XCHG:
	if(args[0]->is_word_reg() && args[1]->is_word_reg()) {
	    if(args[0]->reg==rAX)
		/* xchg ax,r16 */
		w.wb(0x90 + reg_values[args[1]->reg]);
	    else if(args[1]->reg==rAX)
		/* xchg r16,ax */
		w.wb(0x90 + reg_values[args[0]->reg]);
	    else
		/* xchg r16,r16 */
		args[0]->write_rm(w, 0x87, reg_values[args[1]->reg]);
	} else if(args[0]->type==CArgument::REGISTER) {
	    /* xchg r,rm */
	    args[1]->write_rm(w, byte_insn()?0x86:0x87, reg_values[args[0]->reg]);
	} else if(args[1]->type==CArgument::REGISTER) {
	    /* xchg rm,r */
	    args[0]->write_rm(w, byte_insn()?0x86:0x87, reg_values[args[1]->reg]);
	} else
	    throw string("Invalid form of XCHG");
	break;
    case I_MOV:
	if(args[0]->is_seg_reg()) {
	    /* mov Sr,rm16 */
            if (args[1]->is_seg_reg())
                throw string("Invalid assignment of segment register (MOV sr,sr)");
	    args[1]->write_rm(w, 0x8E, reg_values[args[0]->reg]);
	} else if(args[1]->is_seg_reg()) {
	    /* mov rm16,Sr */
	    args[0]->write_rm(w, 0x8C, reg_values[args[1]->reg]);
	} else if(args[0]->is_byte_reg()) {
	    if(args[1]->type==CArgument::IMMEDIATE) {
		/* mov rb,ib */
		w.wb(0xB0 + reg_values[args[0]->reg]);
		w.wb(args[1]->immediate);
	    } else if(args[0]->reg==rAL && args[1]->type==CArgument::MEMORY
		      && args[1]->memory[0]==rNONE && args[1]->memory[1]==rNONE) {
		/* mov al,[disp16] */
		if(args[1]->segment != rDS)
		    seg_override(w, args[1]->segment);
		w.wb(0xA0);
		args[1]->write_immed(w);
	    } else {
		/* mov r8,rm8 */
		args[1]->write_rm(w, 0x8A, reg_values[args[0]->reg]);
	    }
	} else if(args[0]->is_word_reg()) {
	    if(args[1]->type==CArgument::IMMEDIATE) {
		/* mov r16,i16 */
		w.wb(0xB8 + reg_values[args[0]->reg]);
		args[1]->write_immed(w);
	    } else if(args[0]->reg==rAX && args[1]->type==CArgument::MEMORY
		      && args[1]->memory[0]==rNONE && args[1]->memory[1]==rNONE) {
		/* mov ax,[disp16] */
		if(args[1]->segment != rDS)
		    seg_override(w, args[1]->segment);
		w.wb(0xA1);
		args[1]->write_immed(w);
	    } else {
		/* mov r16,rm16 */
		args[1]->write_rm(w, 0x8B, reg_values[args[0]->reg]);
	    }
	} else if(args[0]->type == CArgument::MEMORY) {
	    if(args[1]->is_byte_reg()) {
		if(args[1]->reg==rAL && args[0]->memory[0]==rNONE
		   && args[0]->memory[1]==rNONE) {
		    /* mov [disp16],al */
		    if(args[0]->segment != rDS)
			seg_override(w, args[0]->segment);
		    w.wb(0xA2);
		    args[0]->write_immed(w);
		} else
		    /* mov rm8,r8 */
		    args[0]->write_rm(w, 0x88, reg_values[args[1]->reg]);
	    } else if(args[1]->is_word_reg())
		if(args[1]->reg==rAX && args[0]->memory[0]==rNONE
		   && args[0]->memory[1]==rNONE) {
		    /* mov [disp16],ax */
		    if(args[0]->segment != rDS)
			seg_override(w, args[0]->segment);
		    w.wb(0xA3);
		    args[0]->write_immed(w);
		} else
		    /* mov rm16,r16 */
		    args[0]->write_rm(w, 0x89, reg_values[args[1]->reg]);
	    else if(args[1]->type==CArgument::IMMEDIATE) {
		if(byte_insn()) {
		    /* mov rm8,i8 */
		    args[0]->write_rm(w, 0xC6, 0);
		    w.wb(args[1]->immediate);
		} else {
		    /* mov rm16,i16 */
                    if(do_386 && next && next->insn==I_MOV && next->args[0]->type==CArgument::MEMORY
                       && next->args[1]->type==CArgument::IMMEDIATE
                       && next->opsize == 2) {
                        args[0]->inc_imm(2);
                        if(*args[0] == *next->args[0]) {
                            //cout << "{3}";
                            args[0]->inc_imm(-2);
                            args[0]->write_rm(w, 0xC7, 0, true, 0x66);
                            args[1]->write_immed(w);
                            next->args[1]->write_immed(w);
                            next->ip = ip;
                            return next->next;
                        }
                        args[0]->inc_imm(-2);
                    }
		    args[0]->write_rm(w, 0xC7, 0);
		    args[1]->write_immed(w);
		}
	    } else
		throw string("Invalid MOV to memory");
	} else
	    throw string("Invalid MOV");
	break;
    case I_ROL:
	assemble_shift(w, this, 0);
	break;
    case I_ROR:
	assemble_shift(w, this, 1);
	break;
    case I_RCL:
	assemble_shift(w, this, 2);
	break;
    case I_RCR:
	assemble_shift(w, this, 3);
	break;
    case I_SHL:
	assemble_shift(w, this, 4);
	break;
    case I_SHR:
	assemble_shift(w, this, 5);
	break;
    case I_SAR:
	assemble_shift(w, this, 7);
	break;
    case I_CALLF:
    case I_JMPF:
	if(args[0]->type == CArgument::MEMORY) {
	    /* XXXf m */
	    args[0]->write_rm(w, 0xFF, insn==I_CALLF?3:5);
	} else {
	    if(args[0]->type != CArgument::IMMEDIATE || !args[0]->reloc)
		throw string("Far transfer without relocation");
	    /* XXXf ptr16:16 */
	    if(insn==I_CALLF)
		w.wb(0x9A);
	    else
		w.wb(0xEA);
	    args[0]->write_immed(w);
	    w.wb(0);
	    w.wb(0);
	}
	break;
    case I_RETN:
    case I_RETF:
	if(args[0]->type != CArgument::IMMEDIATE)
	    throw string("Invalid RET");
	{
	    int op = (insn==I_RETN)?0xC2:0xCA;
	    if(args[0]->immediate==0 && !args[0]->reloc)
		/* retX 0 */
		w.wb(op+1);
	    else {
		/* retX imm16 */
		w.wb(op);
		args[0]->write_immed(w);
	    }
	}
	break;
    case I_JCC:
    case I_JCXZ:
	if(args[0]->type != CArgument::LABEL)
	    throw string("JCC/JCXZ to other than label");
	{
	    int distance = args[0]->label->ip - (ip+2);
	    if(distance < -128 || distance > 127) {
		/* jcc near */
                if(insn != I_JCC)
                    throw string("JCXZ too far");
                else if(!do_386) {
                    /* inverse conditional */
                    w.wb(0x71 ^ param);
                    w.wb(3);
                    w.wb(0xE9);
                    distance -= 3;
		    w.wb(distance & 255);
		    w.wb(distance >> 8);
		} else {
		    w.wb(0x0F);
		    w.wb(0x80 + param);
		    distance -= 2;
		    w.wb(distance & 255);
		    w.wb(distance >> 8);
		}
	    } else {
		/* jcc short */
		w.wb((insn==I_JCXZ)?0xE3:(0x70 + param));
		w.wb(distance);
	    }
	}
	break;
    case I_JMPN:
    case I_CALLN:
	if(args[0]->type == CArgument::LABEL) {
	    int distance = args[0]->label->ip - (ip+3);
	    if(insn==I_JMPN && distance>=-129 && distance<=126) {
		/* jmp j8 */
		w.wb(0xEB);
		w.wb(distance+1);
	    } else {
		/* XXX j16 */
		w.wb(insn==I_JMPN?0xE9:0xE8);
		w.wb(distance & 255);
		w.wb(distance >> 8);
	    }
	} else if(args[0]->type==CArgument::IMMEDIATE && args[0]->reloc) {
	    /* XXX j16 */
	    w.wb(insn==I_JMPN?0xE9:0xE8);
	    args[0]->write_immed(w);
	} else if(args[0]->type==CArgument::MEMORY || args[0]->is_word_reg()) {
	    /* XXX rm16 */
	    args[0]->write_rm(w, 0xFF, insn==I_JMPN?4:2);
	} else throw string("Invalid JMP/CALL near");
	break;
     case I_FLAG:
        w.wb(param);
        break;
     case I_STRING:
        if (args[0]->type != CArgument::IMMEDIATE || args[0]->reloc
            || args[1]->type != CArgument::REGISTER)
            throw string("Invalid string insn");
        if (args[0]->immediate) // prefix
            w.wb(args[0]->immediate);
        if (args[1]->reg != rDS)// override
            seg_override(w, args[1]->reg);
        w.wb(param);
        break;
    case I_SETCC:
	if(do_386) {
	    args[0]->write_rm(w, 0x90+param, 0, true, 0x0F);
	} else {
	    if(args[0]->type != CArgument::REGISTER || args[0]->reg<rAL || args[0]->reg>rBL) {
		throw string("Invalid SETCC insn");
	    }
	    /* Codiere SETCC als */
	    /*   mov al,0 */
	    /*   jcc $+3  */
	    /*   inc ax   */
	    w.wb(0xB0 + reg_values[args[0]->reg]);
	    w.wb(0);
	    w.wb(0x71 ^ param);
	    w.wb(1);
	    w.wb(0x40 + reg_values[args[0]->reg]);
	}
	break;
    }
    return next;
}

/*
 *  Liefert true, wenn Änderungen am /modify/ /keep/ beeinflussen
 */
bool alias_reg(TRegister modify, TRegister keep)
{
    if(modify == keep)
        return true;
    switch(modify) {
     case rAX:
        return keep==rAL || keep==rAH;
     case rBX:
        return keep==rBL || keep==rBH;
     case rCX:
        return keep==rCL || keep==rCH;
     case rDX:
        return keep==rDL || keep==rDH;
     case rAL: case rAH:
        return keep==rAX;
     case rBL: case rBH:
        return keep==rBX;
     case rCL: case rCH:
        return keep==rCX;
     case rDL: case rDH:
        return keep==rDX;
     default:
        return false;
    }
}

extern int sys_unit_offset;  /* tputuner.cc */

bool is_call_to (CArgument* a, int index)
{
    if (!a || a->type != CArgument::IMMEDIATE)
        return false;

    register CRelo* r = a->reloc;
    return r && r->unitnum==sys_unit_offset && r->rtype==CODE_PTR_REF
        && r->rblock == index && r->rofs == 0;
}
