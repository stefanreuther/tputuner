/*
 *  Verwaltung von Assembler-Befehlen für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#ifndef INSN_H
#define INSN_H

#include <iostream>
#include "codewriter.h"

class CCodeWriter;

typedef enum { rNONE,
	       rAX, rCX, rDX, rBX, rSP, rBP, rSI, rDI,
	       rAL, rCL, rDL, rBL, rAH, rCH, rDH, rBH,
	       rES, rCS, rSS, rDS,
	       rMAX } TRegister;

extern char reg_sizes[];
extern char reg_values[];
extern TRegister bases[8];
extern TRegister index[8];
extern TRegister def_seg[8];
extern char* reg_names[];

/*** Relozierung ***/
struct CRelo {
    char unitnum, rtype;
    int rblock, rofs;
    CRelo(char* ptr);
    void print(ostream& cout);
    bool operator==(const CRelo& c) const;
};

class CInstruction;

/*** Operand ***/
class CArgument {
public:
    typedef enum { REGISTER, MEMORY, IMMEDIATE, LABEL } TType;
    TType type;
    TRegister reg;         /* REGISTER */
    TRegister memory[2];   /* MEMORY */
    int immediate;         /* MEMORY, IMMEDIATE */
    CRelo* reloc;          /* MEMORY, IMMEDIATE */
    TRegister segment;     /* MEMORY */
    CInstruction* label;   /* LABEL */
    
    CArgument(TRegister areg); /* REGISTER */
    CArgument(TRegister base, TRegister index, int immed=0, CRelo* areloc=0, TRegister seg=rDS);
    CArgument(int immed, CRelo* areloc=0);
    CArgument(CInstruction* l);
    CArgument(const CArgument& arg);
    ~CArgument();

    bool is_word_reg() { return type==REGISTER && reg>=rAX && reg<=rDI; }
    bool is_byte_reg() { return type==REGISTER && reg>=rAL && reg<=rBH; }
    bool is_seg_reg() { return type==REGISTER && reg>=rES && reg<=rDS; }
    bool is_rm() { return type==MEMORY || is_word_reg() || is_byte_reg(); }
    bool is_byte_imm() { return !reloc && (immediate>=-128) && (immediate<=127); }
    bool is_immed(int i) { return type==IMMEDIATE && !reloc && immediate==i; }
    bool is_immed() { return type==IMMEDIATE && !reloc; }
    bool is_reg(TRegister r) { return type==REGISTER && reg==r; }
    bool uses_reg(TRegister r);
    bool uses_reg_part(TRegister r);
    bool affects_reg(TRegister r);

    void write_immed(CCodeWriter& c);
    void write_rm(CCodeWriter& c, int opc, int reg, bool with_seg=true, int prefix=0);

    int get_size();
    void print(ostream& cout);

    bool operator==(const CArgument& a) const;

    void inc_imm(int i) { if(reloc) reloc->rofs+=i; else immediate+=i; }

    int adr_diff(CArgument* other);
};

typedef enum { I_INVALID,
	       I_LABEL,
	       I_MOV, I_LES, I_LDS, I_XCHG, I_LEA,
	       
	       I_POP, I_PUSH,
	       
	       I_DEC, I_INC,
	       I_ADD, I_OR, I_ADC, I_SBB, I_AND, I_SUB, I_XOR, I_CMP,
	       I_IMUL, I_MUL, I_DIV, I_IDIV,
	       I_NOT, I_NEG,

	       I_JCC, I_CALLF, I_CALLN, I_JMPN, I_JMPF, I_RETN, I_RETF,
	       I_JCXZ,
	       
	       I_CBW, I_CWD,

	       I_ROL, I_ROR, I_RCL, I_RCR, I_SHL, I_SHR, I_SAR,
	       
	       I_LEAVE, I_ENTER,

               I_FLAG, I_STRING,

	       I_SETCC
} TInsn;

/* opcodes for certain special insns */
enum {
    CODE_CLD   = 0xFC,
    CODE_STD   = 0xFD,
    CODE_REP   = 0xF3,
    CODE_REPNE = 0xF2,
    CODE_MOVSB = 0xA4,
    CODE_MOVSW = 0xA5,
    CODE_CMPSB = 0xA6,
    CODE_CMPSW = 0xA7,
    CODE_STOSB = 0xAA,
    CODE_STOSW = 0xAB,
    CODE_LODSB = 0xAC,
    CODE_LODSW = 0xAD,
    CODE_SCASB = 0xAE,
    CODE_SCASW = 0xAF
};

enum {		// condition codes
	CC_O,	CC_NO,
	CC_B,	CC_AE,
	CC_E,	CC_NE,
	CC_BE,	CC_A,
	CC_S,	CC_NS,
	CC_PE,	CC_PO,
	CC_L,	CC_GE,
	CC_LE,	CC_G
};

enum {
	IF_ABOVE = 1,
	IF_BELOW = 2,
	IF_EQUAL = 4,
	IF_LESS  = 8,
	IF_GREATER = 16,
        IF_OTHER = 32,
        IF_NONEQUAL
};

extern char* insn_names[];

/*** Befehl ***/
class CInstruction {
public:
    TInsn insn;
    CInstruction* next;
    CInstruction* prev; // nur für die Datenflußanalyse
    CArgument* args[3];
    int opsize;        // Label: 0=unsicher, 1=sicher
    int param;         // optionaler Parameter (cc bei jcc-Sprüngen, Referenzzähler bei Labels
    int ip;
    int temp;
    
    CInstruction(TInsn which, CArgument* a1=0, CArgument* a2=0, CArgument* a3=0);
    ~CInstruction();
    CInstruction* set_os(int os) { opsize = os; return this; }
    CInstruction* set_param(int p) { param = p; return this; }
    CInstruction* set_ip(int p) { ip = p; return this; }
    CInstruction* inc_ref() { param++; return this; }
    CInstruction* dec_ref() { param--; return this; }
    bool byte_insn();

    void print(ostream& cout);
    CInstruction* assemble(CCodeWriter& c);

    bool operator==(const CInstruction& i) const;
};

bool alias_reg(TRegister modify, TRegister keep);
bool is_call_to(CArgument* a, int index);

#endif
