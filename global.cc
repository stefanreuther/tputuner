/*
 *  Globale Optionen für tputuner
 *
 *  (c) copyright 1998,1999,2000 by Stefan Reuther
 */
#include "global.h"

bool do_dfa = true;          // Datenflußanalyse?
bool do_jumpchains = true;   // Sprungketten optimieren?
bool do_peephole = true;     // Befehle ersetzen?
bool do_remunused = true;    // Unbenutzten Code entfernen?
bool do_dumps = false;       // Debug-Dumps
bool do_size = false;        // Auf Größe optimieren
bool do_386 = false;         // 386er Code?
bool do_early_jmp = true;    // Sprünge vorziehen
bool do_late_jmp = true;     // Sprünge nach hinten ziehen
bool do_reg_alloc = false;   // Register-Allokierung
bool do_string_comb = false; // Strings kombinieren
bool do_sort_moves = true;   // mov sortieren
bool do_the_cse = false;     // `cse'
