/*
 *  Globale Optionen für tputuner
 *
 *  (c) copyright 1998,1999,2000 by Stefan Reuther
 */
#include <stdlib.h>
#include <stdio.h>
#include "global.h"

bool do_dfa = true;          // Datenflußanalyse?
bool do_jumpchains = true;   // Sprungketten optimieren?
bool do_peephole = true;     // Befehle ersetzen?
bool do_remunused = true;    // Unbenutzten Code entfernen?
bool do_dumps = false;       // Debug-Dumps
bool do_size = false;        // Auf Größe optimieren
bool do_286 = true;          // 286 code?
bool do_386 = false;         // 386er Code?
bool do_early_jmp = true;    // Sprünge vorziehen
bool do_late_jmp = true;     // Sprünge nach hinten ziehen
bool do_reg_alloc = false;   // Register-Allokierung
bool do_string_comb = false; // Strings kombinieren
bool do_sort_moves = true;   // MOV sortieren
bool do_the_cse = false;     // `cse'
bool do_remframe = false;    // Remove unused stack frames?
bool format_debug = false;   // Additional formatting in debug

char* strdup(const char* s)
{
    size_t len = strlen(s)+1;
    char* d = (char*)malloc(len);
    memcpy(d, s, len);
    return d;
}

char* strndup(const char* s, size_t len)
{
    size_t l = strlen(s);
    if(len > l) len = l;
    char* d = (char*)malloc(len+1);
    memcpy(d, s, len);
    d[len] = 0;
    return d;
}

#ifdef __DJGPP__

int stricmp(const char *s1, const char *s2)
{
    int diff;
    for (diff = 0; *s1 && *s2; s1++, s2++) {
        if (*s1 != *s2)
            if ((diff = (int)tolower(*s1) - (int)tolower(*s2)))
                break;
    }
    return diff;
}

int strnicmp(const char *s1, const char *s2, size_t len) {
    int diff;
    for (diff = 0; len-- && *s1 && *s2; s1++, s2++) {
        if (*s1 != *s2)
            if ((diff = (int)tolower(*s1) - (int)tolower(*s2)))
                break;
    }
    return diff;
}

#endif
