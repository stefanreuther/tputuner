/*
 *  Code-Ausgabe für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 *
 *  Ein Code-Writer bietet Methoden zum Schreiben von Codebytes
 *  und Relozierungseinträgen an. CCWCounter verwirft diese Daten
 *  und zählt sie nur, CCWMemory schreibt sie in ein char-Feld.
 */
#include "codewriter.h"
#include <string>

/*
 *  CodeWriter-Basisklasse
 */
void CCodeWriter::write_word(int i)
{
    wb(i & 255);
    wb(i >> 8);
}

CCWCounter::CCWCounter()
    : bytes(0), relos(0)
{}

/*
 *  Codewriter, der in vorallokierten Speicher schreibt
 */
CCWMemory::CCWMemory(char* acp, int acl, char* arp, int arl, int aip)
    : relo_ptr(arp), code_ptr(acp), relo_left(arl), code_left(acl), ip(aip)
{}

void CCWMemory::wb(char c)
{
    if(!code_left)
	throw string("Code exceeds buffer size");
    *code_ptr++ = c;
    ip++;
    code_left--;
}

void CCWMemory::put_reloc(CRelo* r)
{
    if(relo_left < 8)
	throw string("Relocation buffer overflow");
    *relo_ptr++ = r->unitnum;
    *relo_ptr++ = r->rtype;
    *relo_ptr++ = r->rblock & 255;
    *relo_ptr++ = r->rblock >> 8;
    *relo_ptr++ = r->rofs & 255;
    *relo_ptr++ = r->rofs >> 8;
    *relo_ptr++ = ip & 255;
    *relo_ptr++ = ip >> 8;
    relo_left -= 8;
}
