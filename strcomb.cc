/*
 *  String-Kombination für tputuner
 *
 *  (c) copyright 1999 by Stefan Reuther
 *
 *  Dieses Modul kombiniert Zeichenketten, die mehrfach vorkommen:
 *  - Datenbasis sind alle Codeblocks, deren "Präfix" (Offset 0..entry_ofs)
 *    nur Pascal-Strings enthält
 *  - Gleiche Strings werden kombiniert
 *  - String-Kombination ist nicht möglich, wenn Referenzen auf andere
 *    Adressen als String-Anfänge zeigen
 */

#include <iostream>
#include <string>
#include <map>
#include <set>
#include "strcomb.h"
#include "insn.h"

struct CInfo {
    int code_id;
    int offset;
    CInfo(int aid = -1, int aofs = -1)
        : code_id(aid), offset(aofs) { }
    bool operator<(const CInfo& i) const {
        return (code_id < i.code_id) ||
            (code_id == i.code_id && offset < i.offset);
    }
    bool operator==(const CInfo& i) const {
        return code_id == i.code_id && offset == i.offset;
    }
};

/* sucht in Codeblock p nach Zeichenketten
   - trägt gültige Referenzen in STRS ein
   - meldet Strings in M an */
void check_strings(CCodeBlock* p, map<string,CInfo>& m, set<CInfo>& strs)
{
    p->strcomb_ok = false;
    if(p->status != CCodeBlock::OK || p->entry->entry_ofs==0)
        return;
    
    int index  = 0;
    char* code = (p->new_code ? p->new_code->code : unit + p->code_ofs);
    while(index < p->entry->entry_ofs) {
        int len = (unsigned char)code[index];
        index += len + 1;
    }
    if(index > p->entry->entry_ofs)
        return;

    p->strcomb_ok = true;
    cout << "Code block " << p->id << endl;
    index  = 0;
    while(index < p->entry->entry_ofs) {
        int len = (unsigned char)code[index];
        string s = string(&code[index+1], len);
        if(m.find(s) != m.end()) {
            cout << "  duplicate: `" << s << "'" << endl;
        } else {
            m[s] = CInfo(p->id, index);
        }
        strs.insert(CInfo(p->id, index));
        index += len + 1;
    }
}

/* Prüft die Relokations-Einträge von Codeblock P
   ret FALSE wenn darunter einige sind, die Stringkombinierung unmöglich
   machen */
bool check_string_relo(CCodeBlock* p, const set<CInfo>& inf)
{
    char* relos = p->new_code ? p->new_code->relos : unit + p->relo_ofs;
    int relo_cnt = p->new_code ? p->new_code->relo_size / 8 : p->relo_count;

    while(relo_cnt--) {
        CRelo r(relos);
        if(((unsigned char)r.rtype & 0xC0) == 0x40) {
            /* CS Const */
            // Annahme, daß TP nie solche Referenzen erzeugt, die auf
            // externe Blöcke zeigen
            if(inf.find(CInfo(r.rblock, r.rofs)) == inf.end())
                return false;
        }
        relos += 8;
    }
    return true;
}

/*
 *  String-Kombination
 */
void string_combine(list<CCodeBlock*>& cl)
{
    map<string,CInfo> m;
    set<CInfo> inf;
    for(list<CCodeBlock*>::iterator i = cl.begin();
        i != cl.end();
        i++) {
        check_strings(*i, m, inf);
    }
    for(list<CCodeBlock*>::iterator i = cl.begin();
        i != cl.end();
        i++) {
        if(!check_string_relo(*i, inf)) {
            cout << "String combining not possible -- bad relocation" << endl;
            return;
        }
    }

    map<CInfo, CInfo> replace;
    for(list<CCodeBlock*>::iterator i = cl.begin();
        i != cl.end();
        i++) {
//      reallocate_strings(*i, ...
    }
}
