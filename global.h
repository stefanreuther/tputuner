/*
 *  Globale Optionen für tputuner
 *
 *  (c) copyright 1998 by Stefan Reuther
 */
#ifndef GLOBAL_H
#define GLOBAL_H

extern bool do_dfa;          // Datenflußanalyse?
extern bool do_jumpchains;   // Sprungketten optimieren?
extern bool do_peephole;     // Befehle ersetzen?
extern bool do_remunused;    // Unbenutzten Code entfernen?
extern bool do_dumps;        // Debug-Dumps?
extern bool do_size;         // Auf Größe optimieren
extern bool do_386;          // 386er Code erlauben?
extern bool do_early_jmp;    // Sprünge vorziehen
extern bool do_late_jmp;     // Sprünge nach hinten ziehen
#endif
