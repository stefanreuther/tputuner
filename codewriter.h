/*
 *  Code-Ausgabe für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#ifndef CODEWRITER_H
#define CODEWRITER_H

#include "insn.h"

class CRelo;

class CCodeWriter {
 public:
    virtual void wb(char c) = 0;
    void write_word(int i);
    virtual void put_reloc(CRelo*) = 0;
    CCodeWriter() {}
    virtual ~CCodeWriter() {}
};

/* Dummy-Schreiber, zum Berechnen der Größe des Codes */
class CCWCounter : public CCodeWriter {
 public:
    int bytes;
    int relos;
    CCWCounter();
    ~CCWCounter() {}
    void wb(char c) { bytes++; }
    void put_reloc(CRelo*) { relos++; }
};

/* Code in reservierten Speicherblock schreiben */
class CCWMemory : public CCodeWriter {
 public:
    char* relo_ptr;
    char* code_ptr;
    int relo_left;
    int code_left;
    int ip;
    CCWMemory(char* acp, int acl, char* arp, int arl, int aip);
    ~CCWMemory() {}
    void wb(char c);
    void put_reloc(CRelo*);
};

#endif
