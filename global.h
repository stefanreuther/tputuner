/*
 *  Globale Optionen für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#ifndef GLOBAL_H
#define GLOBAL_H

#include <string>

extern bool do_dfa;          // Datenflußanalyse?
extern bool do_jumpchains;   // Sprungketten optimieren?
extern bool do_peephole;     // Befehle ersetzen?
extern bool do_remunused;    // Unbenutzten Code entfernen?
extern bool do_dumps;        // Debug-Dumps?
extern bool do_size;         // Auf Größe optimieren
extern bool do_386;          // 386er Code erlauben?
extern bool do_early_jmp;    // Sprünge vorziehen
extern bool do_late_jmp;     // Sprünge nach hinten ziehen
extern bool do_reg_alloc;    // Register-Allokierung
extern bool do_string_comb;  // Strings kombinieren
extern bool do_sort_moves;   // movs sortieren
extern bool do_the_cse;      // `CSE'

class CCodeBlock;
class CNewCode;

/* Implementierung der Klassen in tputuner.cc */

/*
 *  CEntryBlock
 *  repräsentiert einen Entry-Block
 */
class CEntryBlock {
public:
    int ofs;             // Position in unit
    int entry_ofs;       // Offset in Codeblock
    string name;
    char flags;          // Flags für Codeblock
    CCodeBlock* code;    // Codeblock
    CEntryBlock(int aofs, CCodeBlock* acode);
};

/*
 *  CCodeBlock
 *  Ein Code-Block (=Prozedur, OBJ-Datei)
 */
class CCodeBlock {
public:
    typedef enum { UNUSED, OK, MULTI_ENTRY } TStatus;
    int ofs;            // Position des Codeblock-Headers
    int code_ofs;       // Position des Maschinencodes
    int code_size;      // Größe desselben
    int relo_ofs;       // Position der Relo-Entries
    int relo_count;     // Anzahl Relo-Entries

    CNewCode* new_code; // neuer Code oder 0 wenn nicht geändert

    CEntryBlock* entry; // Entry-Block, falls bekannt und eindeutig
    TStatus status;     // Status
    int id;             // ID = Codeblock-Nummer (0, 8, 16, ...)

    bool strcomb_ok;    // String-Kombination möglich?
    
    CCodeBlock(int aofs, int& code_base, int& relo_base, int aid);
    void SetEntryBlock(CEntryBlock*);
    void optimize();
    bool check_relo(int ofs);
};

extern char* unit;

/* optimize.cc */
struct CNewCode {
    char* code;
    int   code_size;

    char* relos;
    int   relo_size;
};

#endif
