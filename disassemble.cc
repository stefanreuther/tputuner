/*
 *  Disassembler für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#include <string>
#include <cstdio>
#include "insn.h"
#include "disassemble.h"
#include "global.h"

char* relos;
int relo_count;
int next_relo;
int ip, ip_diff;
int max_ip;
char* code;

/* Status des Disassemblers */
int last_reg;      /* reg-Feld vom modr/m */
TRegister seg_ovr; /* Segment-Override */

CInstruction* first_insn;
CInstruction* labels_left;

/*
 *  Erste Relokation setzen
 */
void set_relo(char* arelos, int arelo_count)
{
    relos = arelos;
    relo_count = arelo_count;
    if(relo_count==0)
	next_relo = -1;
    else
	next_relo = (unsigned char)(relos[6]) + 256*(unsigned char)(relos[7]);
}

/*
 *  liefert Relokation an dieser Position und setzt Zeiger eins weiter
 */
char* relo_at(int ip)
{
    if(ip == next_relo) {
	char* p = relos;
	relo_count--;
	if(relo_count==0) {
	    next_relo = -1;
	} else {
	    relos += 8;
	    next_relo = (unsigned char)(relos[6]) + 256*(unsigned char)(relos[7]);
	}
	return p;
    }
    if(ip > next_relo && next_relo!=-1) {
	char message[100];
	sprintf(message,"Unrecognized relocation item at 0x%04x!", next_relo);
	throw string(message);
    }
    return 0;
}

/*
 *  Liefert Zeiger auf ein Label, das an at_ip platziert wird
 */
CInstruction* label_at(int at_ip)
{
    if(ip > at_ip) {
	/* Label muß im bereits disassemblierten Bereich liegen */
	CInstruction* p = first_insn;
	CInstruction* q = 0;
	while(p && p->ip < at_ip) q=p, p=p->next;
	if(p && p->ip > at_ip) throw string("Invalid code - jump into instruction");
	if(p && p->insn==I_LABEL) return p->inc_ref();

	/* neu erzeugen */
	CInstruction* t = (new CInstruction(I_LABEL))->set_ip(at_ip);
	t->param = 1; // Referenzzähler
	if(q)
	    t->next = q->next, q->next = t;
	else
	    t->next = first_insn, first_insn = t;
	return t;
    } else {
	/* Vorwärtsreferenz */
	CInstruction* p = labels_left;
	CInstruction* q = 0;
	while(p && p->ip < at_ip) q=p, p=p->next;
	if(p && p->ip==at_ip) return p->inc_ref();

	/* neu erzeugen */
	CInstruction* t = (new CInstruction(I_LABEL))->set_ip(at_ip);
	t->param = 1;
	if(q)
	    t->next=q->next, q->next = t;
	else
	    t->next=labels_left, labels_left = t;
	return t;
    }
}

//
//  Liest ein signed-word aus dem Datenstrom
//
int read_word()
{
    int i;
    i = (unsigned char)code[ip++ - ip_diff];
    i += 256 * (unsigned char)code[ip++ - ip_diff];
    if(i>32767) i-=65536;
    return i;
}

//
//  Liest ein signed-byte aus dem Datenstrom
//
int read_byte()
{
    return (signed char)code[ip++ - ip_diff];
}

//
//  Holt einen iw-Operanden (imm16)
//
CArgument* get_immed_word()
{
    char* relo = relo_at(ip);
    if(relo) {
	read_word();
	return new CArgument(0, new CRelo(relo));
    } else
	return new CArgument(read_word());
}

//
//  Holt einen ib-Operanden (imm8)
//
CArgument* get_immed_byte()
{
    return new CArgument(read_byte());
}

TRegister word_regs[8] = { rAX, rCX, rDX, rBX, rSP, rBP, rSI, rDI };
TRegister byte_regs[8] = { rAL, rCL, rDL, rBL, rAH, rCH, rDH, rBH };
TRegister seg_regs[8] = { rES, rCS, rSS, rDS, rNONE, rNONE, rNONE, rNONE };
TInsn arit_insns[8] = { I_ADD, I_OR, I_ADC, I_SBB, I_AND, I_SUB, I_XOR, I_CMP };
TInsn shift_insns[8] = { I_ROL, I_ROR, I_RCL, I_RCR, I_SHL, I_SHR, I_SHL, I_SAR };
TInsn F6_insns[8] = { I_INVALID, I_INVALID, I_NOT, I_NEG, I_MUL, I_IMUL, I_DIV, I_IDIV };
TInsn FF_insns[8] = { I_INC, I_DEC, I_CALLN, I_CALLF, I_JMPN, I_JMPF, I_PUSH, I_INVALID };

CInstruction* decode_arit(TInsn op, int opc);
CInstruction* rm_r_insn(TInsn op, TRegister* regs);
CInstruction* r_rm_insn(TInsn op, TRegister* regs);
CArgument* decode_mod_rm(TRegister* regs);
CArgument* disp16_arg();

//
//  386er Instruktion decodieren
//
CInstruction* decode_386()
{
    unsigned char opcode = read_byte();

    if(opcode >= 0x80 && opcode <= 0x8F) {
	char* relo = relo_at(ip);
	int w;
	CArgument* a;
	if(!relo) {
	    w = read_word();
	    a = new CArgument(label_at(ip + w));
	} else {
	    read_word();
	    a = new CArgument(0, new CRelo(relo));
	}
	return (new CInstruction(I_JCC, a))->set_param(opcode & 15);
    } else if(opcode >= 0x90 && opcode <= 0x9F) {
	return (new CInstruction(I_SETCC, decode_mod_rm(byte_regs)))
	    ->set_param(opcode & 15)->set_os(1);
    } else
	return 0;
}

//
//  Eine Instruktion decodieren
//
CInstruction* decode_one(int& opcode_ip)
{
    seg_ovr = rNONE;
    unsigned char opcode;
    CArgument* a;
    char* relo;
    int w;
    int repeat = 0;

    while(1) {
	opcode_ip = ip;
	opcode = code[ip++ - ip_diff];
	if(opcode == 0x26) seg_ovr = rES;
	else if(opcode==0x36) seg_ovr = rSS;
	else if(opcode==0x2E) seg_ovr = rCS;
	else if(opcode==0x3E) seg_ovr = rDS;
        else if(opcode==0xF2 || opcode==0xF3) repeat = opcode;
	else break;
    }

    if (repeat && !((opcode >= 0xA4 && opcode <= 0xA7)
                    || (opcode >= 0xAA && opcode <= 0xAF)))
        throw string("Invalid insn with repeat prefix");

    switch(opcode / 8) {
    case 0: // ADD
	if(opcode == 0x06) return new CInstruction(I_PUSH,
						   new CArgument(rES));
	if(opcode == 0x07) return new CInstruction(I_POP,
						   new CArgument(rES));
	return decode_arit(I_ADD, opcode);
    case 1: // OR
	if(opcode == 0x0E) return new CInstruction(I_PUSH,
						   new CArgument(rCS));
	if(opcode == 0x0F) {
	    if(do_386) return decode_386();
	    else return 0;
	}
	return decode_arit(I_OR, opcode);
    case 2: // ADC
	if(opcode == 0x16) return new CInstruction(I_PUSH,
						   new CArgument(rSS));
	if(opcode == 0x17) return new CInstruction(I_POP,
						   new CArgument(rSS));
	return decode_arit(I_ADC, opcode);
    case 3: // SBB
	if(opcode == 0x1E) return new CInstruction(I_PUSH,
						   new CArgument(rDS));
	if(opcode == 0x07) return new CInstruction(I_POP,
						   new CArgument(rDS));
	return decode_arit(I_SBB, opcode);
    case 4: // AND
	return decode_arit(I_AND, opcode);
    case 5: // SUB
	return decode_arit(I_SUB, opcode);
    case 6: // XOR
	return decode_arit(I_XOR, opcode);
    case 7: // CMP
	return decode_arit(I_CMP, opcode);
    case 8: // INC
	return new CInstruction(I_INC,
				new CArgument(word_regs[opcode & 7]));
    case 9: // DEC
	return new CInstruction(I_DEC,
				new CArgument(word_regs[opcode & 7]));

    case 10: // PUSH
	return new CInstruction(I_PUSH,
				new CArgument(word_regs[opcode & 7]));
    case 11: // POP
	return new CInstruction(I_POP,
				new CArgument(word_regs[opcode & 7]));
    case 12: // 286/386  60..67
	return 0;
    case 13: // 286/386  68..6F
	switch(opcode) {
	case 0x68: // PUSH iw
	    return new CInstruction(I_PUSH, get_immed_word());
	case 0x69: // IMUL r16,rm16,iw
	    a = decode_mod_rm(word_regs);
	    return new CInstruction(I_IMUL,
				    new CArgument(word_regs[last_reg]),
				    a,
				    get_immed_word());
	case 0x6A: // PUSH ib
	    return new CInstruction(I_PUSH, get_immed_byte());
	case 0x6B: // IMUL r16,rm16,ib
	    a = decode_mod_rm(word_regs);
	    return new CInstruction(I_IMUL,
				    new CArgument(word_regs[last_reg]),
				    a,
				    get_immed_byte());
	// der Rest sind Block I/O Insns
	}
	return 0;
    case 14: // Jcc
    case 15: // Jcc
	w = read_byte();
	return (new CInstruction(I_JCC, new CArgument(label_at(w + ip))))
	    ->set_param(opcode & 15);
    case 16: // 80..87
	switch(opcode) {
	case 0x80:
	case 0x82:
	    a = decode_mod_rm(byte_regs);
	    return (new CInstruction(arit_insns[last_reg],
				     a,
				     get_immed_byte()))->set_os(1);
	case 0x81:
	    a = decode_mod_rm(word_regs);
	    return (new CInstruction(arit_insns[last_reg],
				     a,
				     get_immed_word()))->set_os(2);
	case 0x83:
	    a = decode_mod_rm(word_regs);
	    return (new CInstruction(arit_insns[last_reg],
				     a,
				     get_immed_byte()))->set_os(2);
	    // es folgen test/xchg
	}
	return 0;
    case 17: // 88..8F
	switch(opcode) {
	case 0x88: // MOV rm8,r8
	    return rm_r_insn(I_MOV, byte_regs);
	case 0x89: // MOV rm16,r16
	    return rm_r_insn(I_MOV, word_regs);
	case 0x8A: // MOV r8,rm8
	    return r_rm_insn(I_MOV, byte_regs);
	case 0x8B: // MOV r16,rm16
	    return r_rm_insn(I_MOV, word_regs);
	case 0x8C: // MOV rm16,Sr
	    a = decode_mod_rm(word_regs);
	    return new CInstruction(I_MOV, a, new CArgument(seg_regs[last_reg]));
	case 0x8D: // LEA r16,rm16
	    return r_rm_insn(I_LEA, word_regs);
	case 0x8E: // MOV Sr,rm16
	    a = decode_mod_rm(word_regs);
	    return new CInstruction(I_MOV, new CArgument(seg_regs[last_reg]), a);
	case 0x8F: // POP rm16
	    return (new CInstruction(I_POP, decode_mod_rm(word_regs)))->set_os(2);
	}
	return 0;
    case 18: // XCHG AX,
	return new CInstruction(I_XCHG,
			        new CArgument(rAX),
				new CArgument(word_regs[opcode & 7]));
    case 19: // 98..9F
	switch(opcode) {
	case 0x98:
	    return new CInstruction(I_CBW);
	case 0x99:
	    return new CInstruction(I_CWD);
	case 0x9A:
	    relo = relo_at(ip);
	    if(!relo) throw string("Can't handle CALLF to absolute address");
	    ip += 4;
	    return new CInstruction(I_CALLF, new CArgument(0, new CRelo(relo)));
	case 0x9B: // WAIT
	    throw string("FPU is not supported");
	}
	return 0;
    case 20: // A0..A7
	switch(opcode) {
	case 0xA0:
	    return new CInstruction(I_MOV,
				    new CArgument(rAL),
				    disp16_arg());
	case 0xA1:
	    return new CInstruction(I_MOV,
				    new CArgument(rAX),
				    disp16_arg());
	case 0xA2:
	    return new CInstruction(I_MOV,
				    disp16_arg(),
				    new CArgument(rAL));
	case 0xA3:
	    return new CInstruction(I_MOV,
				    disp16_arg(),
				    new CArgument(rAX));
         case 0xA4: case 0xA5:  // MOVS
         // case 0xA6: case 0xA7:  // CMPS
            return (new CInstruction(I_STRING,
                                     new CArgument(repeat, 0),
                                     new CArgument(seg_ovr == rNONE ? rDS : seg_ovr)))
                ->set_param(opcode);
	}
	return 0;
    case 21: // A8-AF: TEST, Stringbefehle
       switch(opcode) {
        case 0xAA: case 0xAB:   // STOS
        case 0xAC: case 0xAD:   // LODS
        // case 0xAE: case 0xAF:   // SCAS
           return (new CInstruction(I_STRING,
                                    new CArgument(repeat, 0),
                                    new CArgument(seg_ovr == rNONE ? rDS : seg_ovr)))
               ->set_param(opcode);
       }
       return 0;
    case 22: // MOV rb,ib
	return new CInstruction(I_MOV,
				new CArgument(byte_regs[opcode & 7]),
				get_immed_byte());
    case 23: // MOV rw,iw
	return new CInstruction(I_MOV,
				new CArgument(word_regs[opcode & 7]),
				get_immed_word());
    case 24: // C0..C7
	switch(opcode) {
	case 0xC0:
	    a = decode_mod_rm(byte_regs);
	    return (new CInstruction(shift_insns[last_reg],
				     a,
				     get_immed_byte()))->set_os(1);
	case 0xC1:
	    a = decode_mod_rm(word_regs);
	    return (new CInstruction(shift_insns[last_reg],
				     a,
				     get_immed_byte()))->set_os(2);
	case 0xC2:
	    return new CInstruction(I_RETN, get_immed_word());
	case 0xC3:
	    return new CInstruction(I_RETN, new CArgument(0));
	case 0xC4:
	    return r_rm_insn(I_LES, word_regs);
	case 0xC5:
	    return r_rm_insn(I_LDS, word_regs);
	case 0xC6:
	    a = decode_mod_rm(byte_regs);
	    return (new CInstruction(I_MOV, a, get_immed_byte()))->set_os(1);
	case 0xC7:
	    a = decode_mod_rm(word_regs);
	    return (new CInstruction(I_MOV, a, get_immed_word()))->set_os(2);
	}
	return 0;
    case 25: // C8..CF
	switch(opcode) {
	case 0xC8:
	    a = get_immed_word();
	    return new CInstruction(I_ENTER, a, get_immed_byte());
	case 0xC9:
	    return new CInstruction(I_LEAVE);
	case 0xCA:
	    return new CInstruction(I_RETF, get_immed_word());
	case 0xCB:
	    return new CInstruction(I_RETF, new CArgument(0));
	case 0xCC:
	case 0xCD:
	case 0xCE:
	case 0xCF:
	    throw string("Direct interrupt access");
	}
	return 0;
    case 26: // D0..D7
	switch(opcode) {
	case 0xD0:
	    a = decode_mod_rm(byte_regs);
	    return (new CInstruction(shift_insns[last_reg],
				     a,
				     new CArgument(1)))->set_os(1);
	case 0xD1:
	    a = decode_mod_rm(word_regs);
	    return (new CInstruction(shift_insns[last_reg],
				     a,
				     new CArgument(1)))->set_os(2);
	case 0xD2:
	    a = decode_mod_rm(byte_regs);
	    return (new CInstruction(shift_insns[last_reg],
				     a,
				     new CArgument(rCL)))->set_os(1);
	case 0xD3:
	    a = decode_mod_rm(word_regs);
	    return (new CInstruction(shift_insns[last_reg],
				     a,
				     new CArgument(rCL)))->set_os(2); // weil os(CL) == 1
	    // es folgen AA{M,D}, SETALC, XLAT
	}
	return 0;
    case 27: // Copro
	throw string("FPU is not supported.");
    case 28: // E0..E7: Schleifen, I/O
	switch(opcode) {
	case 0xE3:
	    w = read_byte();
	    return new CInstruction(I_JCXZ, new CArgument(label_at(w + ip)));
	}
	return 0;
    case 29: // E8..EF
	switch(opcode) {
	case 0xE8:
	case 0xE9:
	    relo = relo_at(ip);
	    if(!relo) {
		w = read_word();
		a = new CArgument(label_at(ip + w));
	    } else {
		read_word();
		a = new CArgument(0, new CRelo(relo));
	    }
	    return new CInstruction((opcode==0xE8)?I_CALLN:I_JMPN, a);
	case 0xEA:
	    relo = relo_at(ip);
	    if(!relo) throw string("Can't handle JMPF to absolute address");
	    ip += 4;
	    return new CInstruction(I_JMPF, new CArgument(0, new CRelo(relo)));
	case 0xEB:
	    w = read_byte();
	    return new CInstruction(I_JMPN, new CArgument(label_at(ip + w)));
	    // es folgt I/O
	}
	return 0;
    case 30: // F0..F7
	switch(opcode) {
	case 0xF6:
	    a = decode_mod_rm(byte_regs);
	    if(F6_insns[last_reg] == I_INVALID)
		return 0;
	    else
		return (new CInstruction(F6_insns[last_reg], a))->set_os(1);
	case 0xF7:
	    a = decode_mod_rm(word_regs);
	    if(F6_insns[last_reg] == I_INVALID)
		return 0;
	    else
		return (new CInstruction(F6_insns[last_reg], a))->set_os(2);
	}
	return 0;
    case 31: // F8..FF
	switch(opcode) {
         case 0xFC:
         case 0xFD:
            /* cld, std */
            return (new CInstruction(I_FLAG))->set_param(opcode);
	case 0xFE:
	    a = decode_mod_rm(byte_regs);
	    if(last_reg==0)
		return (new CInstruction(I_INC, a))->set_os(1);
	    else if(last_reg==1)
		return (new CInstruction(I_DEC, a))->set_os(1);
	    else
		return 0;
	case 0xFF:
	    a = decode_mod_rm(word_regs);
	    if(FF_insns[last_reg]==I_INVALID) return 0;
	    return (new CInstruction(FF_insns[last_reg], a))->set_os(2);
	}
	return 0;
    }
    return 0;
}

//
//  Eine Funktion decodieren
//
CInstruction* disassemble(char* acode, int code_size, int my_offset,
			  char* _relo, int _relo_count)
{
    CInstruction* last = 0;
    first_insn = 0;
    labels_left = 0;

    code = acode;
    max_ip = my_offset + code_size;
    ip = ip_diff = my_offset;
    set_relo(_relo, _relo_count);

    while(ip < max_ip) {
	int ip_save = ip;
	int ip_opcode;  // kann sich von ip_save unterscheiden wegen Präfixen
	CInstruction* p = decode_one(ip_opcode);
	if(!p) {
	    char message[100];
	    sprintf(message, "Unrecognized opcode %02x!", (unsigned char)code[ip_opcode - ip_diff]);
	    throw string(message);
	}
	p->set_ip(ip_save);

	if(last) last->next=p;
	else first_insn=p;
	p->next=0;
	last=p;
	if(labels_left) {
	    if(labels_left->ip == ip) {
		last->next = labels_left;
		labels_left = labels_left->next;
		last = last->next;
		last->next = 0;
	    } else if(labels_left->ip < ip) {
		throw string("Invalid code - jump target not recognized.");
	    }
	}
    }
    if(labels_left || last->insn==I_LABEL) throw string("Invalid code - label outside code.");

    return first_insn;
}

//
//  Einen Arithmetikbefehl decodieren
//
CInstruction* decode_arit(TInsn op, int opc)
{
    switch(opc & 7) {
    case 0: // op rm8,r8
	return rm_r_insn(op,byte_regs);
    case 1: // op rm16,r16
	return rm_r_insn(op,word_regs);
    case 2: // op r8,rm8
	return r_rm_insn(op,byte_regs);
    case 3: // op r16,rm16
	return r_rm_insn(op,word_regs);
    case 4: // op AL,ib
	return new CInstruction(op,
				new CArgument(rAL),
				get_immed_byte());
    case 5: // op AX,iw
	return new CInstruction(op,
				new CArgument(rAX),
				get_immed_word());
    }
    return 0;
}

//
//  Einen Befehl der Form "op rm,r" decodieren
//
CInstruction* rm_r_insn(TInsn op, TRegister* regs)
{
    CArgument* a = decode_mod_rm(regs);
    return new CInstruction(op, a, new CArgument(regs[last_reg]));
}

//
//  Einen Befehl der Form "op r,rm" decodieren
//
CInstruction* r_rm_insn(TInsn op, TRegister* regs)
{
    CArgument* a = decode_mod_rm(regs);
    return new CInstruction(op, new CArgument(regs[last_reg]), a);
}

//
//  Ein disp16-Argument [xxxx]
//
CArgument* disp16_arg()
{
    char* relo = relo_at(ip);
    int disp = read_word();

    if(seg_ovr==rNONE) seg_ovr=rDS;
    if(relo)
	return new CArgument(rNONE, rNONE, 0, new CRelo(relo), seg_ovr);
    else
	return new CArgument(rNONE, rNONE, disp, 0, seg_ovr);
}

//
//  Ein modr/m Byte decodieren
//
CArgument* decode_mod_rm(TRegister* regs)
{
    unsigned char mod_rm = read_byte();

    last_reg = (mod_rm >> 3) & 7;

    /* Registeroperanden */
    if((mod_rm & 0xC0U) == 0xC0U) {
	return new CArgument(regs[mod_rm & 7]);
    }

    /* Speicherreferenz [iw] */
    if((mod_rm & 0xC7U) == 6) {
	return disp16_arg();
    }

    /* Standard-Speicherreferenzen */
    char* relo = 0;
    int disp = 0;
    if((mod_rm & 0xC0) == 0x40) {  // disp8
	disp = read_byte();
    } else if((mod_rm & 0xC0) == 0x80) { // disp16
	relo = relo_at(ip);
	disp = read_word();
    }
    if(seg_ovr==rNONE) seg_ovr = def_seg[mod_rm & 7];
    if(relo) disp=0;
    return new CArgument(bases[mod_rm & 7], index[mod_rm & 7], disp,
			 relo?new CRelo(relo):0, seg_ovr);
}
