/*
 *  tputuner - Ein Programm zur Optimierung von Turbo-Pascal Code
 *
 *  (c) copyright 1998,1999,2000,2002 by Stefan Reuther
 *
 *  Hauptmodul. Hier wird die Unit geladen, die Code-, Entry- und
 *  Relocation-Einträge geladen und zugeordnet, und schließlich
 *  die Optimierung gestartet.
 */
#include <cstdio>
#include <iostream>
#include <list>
#include <string>
#include <cstring>       /* für Parameterparsing... */
#include "tpufmt.h"
#include "optimize.h"
#include "global.h"
#include "strcomb.h"

char* unit;        // Original-Unit
int unit_size;     // Originalgröße

char* new_unit;    // Neue Unit
int new_size;      // Neue Größe

int ofs_this_unit = 0; // Offset, unter dem diese Unit referenziert wird
int ref_this_unit = 0;
int sys_unit_offset = 0;

bool do_names = true;  // Namen lesen?

// Holt ein 16-Bit Datum aus der Original-Unit
inline int get_word(int ofs)
{
    return (unsigned char)(unit[ofs]) + 256*(unsigned char)(unit[ofs+1]);
}

// Holt ein 16-Bit Datum aus der neuen Unit
inline int nget_word(int ofs)
{
    return (unsigned char)(new_unit[ofs]) + 256*(unsigned char)(new_unit[ofs+1]);
}

// Setzt ein 16-Bit Datum in die neue Unit
inline void put_word(int ofs, int w)
{
    new_unit[ofs++] = w & 255;
    new_unit[ofs] = w >> 8;
}

list<CEntryBlock*> entry_list;
list<CCodeBlock*> code_list;

/*
 *  CEntryBlock
 */
CEntryBlock::CEntryBlock(int aofs, CCodeBlock* acode)
    : ofs(aofs), flags(0), code(acode)
{
    code->SetEntryBlock(this);
    entry_ofs = get_word(ofs+6);
}

/*
 *  CCodeBlock
 */
CCodeBlock::CCodeBlock(int aofs, int& code_base, int& relo_base, int aid)
    : ofs(aofs), new_code(0), entry(0), status(UNUSED), id(aid)
{
    code_ofs = code_base;
    code_size = get_word(ofs+2);
    relo_ofs = relo_base;
    relo_count = get_word(ofs+4) / 8;

    code_base += code_size;
    relo_base += relo_count * 8;
}

// `Entry-Block p möchte Code aus diesem Codeblock'
void CCodeBlock::SetEntryBlock(CEntryBlock* p)
{
    if(status==UNUSED) {
        status=OK;
	entry=p;
    } else
	status=MULTI_ENTRY;
}

void showc(unsigned char c)
{
    if(c>=32) cout << c;
    else cout << "{" << (int)c << "}";
}

// Relozierung an Offset ofs überprüfen
bool CCodeBlock::check_relo(int ofs)
{
    switch((unsigned char)(unit[ofs+1] & 0xC0)) {
    case 0x40:
	/* CS const: darf nicht in den Code zeigen, da sich der
	 * Offset während der Optimierung ändern wird */
	if(get_word(ofs+4) >= entry->entry_ofs)
	    return false;
	break;
// verlassen wir uns lieber drauf, daß TP solche Records nie erzeugt.
// sonst würden auch OK'ne Blöcke zurückgewiesen.
//    case 0x00:
//	/* Code: darf nicht auf den Codeblock selbst zeigen (und damit
//	 * u.U. in den Code hinein) */
//	if(get_word(ofs+2) == id) /* relo zeigt in diesen Block */
//	    return false;
//	break;
    }
    /* Relozierung darf nicht im CS-Const-Bereich liegen. TP erzeugt */
    /* meines Wissens keine solchen Relozierungen, aber ... */
    if(get_word(ofs+6) < entry->entry_ofs) return false;
    
    return true;
}


void CCodeBlock::optimize()
{
    /* Prüfe Relozierungen */
    for(int i=0; i<relo_count; i++) {
	if(!check_relo(relo_ofs + 8*i)) {
	    cout << "Error: bad relocation entries (no. #" << i << ") -- skipping"
		 << endl;
	    return;
	}
    }

    /* Optimieren */
    try {
	new_code = do_optimize(id,
			       unit + code_ofs /*+ entry->entry_ofs*/, code_size - entry->entry_ofs,
			       entry->entry_ofs,
			       unit + relo_ofs, relo_count);

	if(new_code) {
	    /* Optimierung OK: Code und CS-Consts verbinden */
	    char* c = new char[new_code->code_size + entry->entry_ofs];
	    char* p = c;
	    for(int i=0; i<entry->entry_ofs; i++) // CS Consts
		*p++ = unit[code_ofs + i];
	    for(int i=0; i<new_code->code_size; i++) // Neuer Code
		*p++ = new_code->code[i];
	    delete[] new_code->code;
	    new_code->code = c;
	    new_code->code_size += entry->entry_ofs;
	}
    }
    catch(string& s) {
	/* Fehler werfen strings */
	cout << s << endl;
	new_code = 0;
    }
}

//
// Lädt die durch filename angegebene Datei
//
bool load_unit(char* filename)
{
    FILE* fp = fopen(filename,"rb");
    if(!fp) {
	cerr << "Error: could not open file " << filename << endl;
	return false;
    }

    if(fseek(fp, 0, SEEK_END) != 0) {
	cerr << "Error: file seems not to be seekable" << endl;
	return false;
    }
    unit_size = ftell(fp);
    if(unit_size<0) {
	cerr << "Error: file seems not to be seekable" << endl;
	return false;
    }

    unit = new char[unit_size];
    fseek(fp, 0, SEEK_SET);
    fread(unit, 1, unit_size, fp);
    fclose(fp);

    if(strncmp(unit, UNIT_ID, 9) != 0) {
	cout << "Error: not a `" UNIT_ID "' file" << endl;
	delete[] unit;
	return false;
    }

    ref_this_unit = ofs_this_unit = get_word(OFS_THIS_UNIT) + 2;
    if(unit[ofs_this_unit] != ITYP_UNIT) {
        cout << "Error: Can't find internal name of unit" << endl;
        delete[] unit;
        return false;
    }

    ofs_this_unit = ofs_this_unit + 2 + unit[ofs_this_unit+1];
    
    return true;
}

// Zahl auf angebrochene Paragraphen runden
int roundup(int n)
{
    return ((n-1) | 15) + 1;
}

static inline int browser_size()
{
#if UNIT_FORMAT == 7
    return roundup(get_word(BROWSER_SIZE));
#else
    return 0;
#endif
}

/*
 *  Liest eine Symbolliste aus der Unit
 */
void read_hashtable(int hash_ofs, string prefix);
void read_hash_branch(int ofs, string prefix)
{
    while(ofs) {
        char type = unit[ofs+2];
        string name(unit+ofs+4, (int)unit[ofs+3]);
        int data_ptr = ofs + unit[ofs+3] + 4;

        switch(type) {
         case ITYP_PROC: // Prozedur
            {
                int entry_ofs = get_word(OFS_ENTRY_PTS) + get_word(data_ptr+2);
                for(list<CEntryBlock*>::iterator i=entry_list.begin();
                    i!=entry_list.end(); i++) {
                    if((*i)->ofs == entry_ofs) {
                        (*i)->name = prefix + name;
                        (*i)->flags = unit[data_ptr];
                    }
                }
            }
            break;
         case ITYP_TYPE: // Typ
            {
                int type_ofs = get_word(data_ptr);
                if(get_word(data_ptr+2)==ofs_this_unit
                   && unit[type_ofs]==3) {
                    // Objekttyp aus dieser Unit
                    read_hashtable(get_word(type_ofs + TYPE_HASH_OFS), prefix + name + ".");
                }
            }
            break;
        }
        
        ofs = get_word(ofs);
    }
}

/*
 *  Liest die Haupt-Hashtable der Unit
 */
void read_hashtable(int hash_ofs, string prefix)
{
    int hash_size = get_word(hash_ofs) / 2;

    for(int i=1; i<=hash_size; i++)
        read_hash_branch(get_word(hash_ofs + 2*i), prefix);
}

void find_system_unit()
{
    int ofs = get_word(OFS_UNIT_LIST);
    int end = get_word(OFS_SRC_NAME);
    while(ofs < end) {
        int len = unit[ofs + UNIT_BLOCK_NAME];
        if(len == 6 && (strncmp(&unit[ofs + UNIT_BLOCK_NAME+1], "SYSTEM", 6)==0
                        || strncmp(&unit[ofs + UNIT_BLOCK_NAME+1], "System", 6)==0)) {
            sys_unit_offset = ofs - get_word(OFS_UNIT_LIST);
            cout << "System unit at " << sys_unit_offset << endl;
            return;
        }
        ofs += len + UNIT_BLOCK_NAME+1;
    }
    cout << "Can't find system unit!" << endl;
}

/*
 *  Objekte aus der Unitdatei lesen
 */
void create_objects()
{
    // die Code-Blocks
    int codeblock_ofs = get_word(OFS_CODE_BLOCKS);
    int codeblock_limit = get_word(OFS_CONST_BLOCKS);
    int code_base     = roundup(get_word(SYM_SIZE)) + browser_size();
    int relo_base     = code_base + roundup(get_word(CODE_SIZE)) + roundup(get_word(CONST_SIZE));

    int id = 0;
    while(codeblock_ofs < codeblock_limit) {
	code_list.push_back(new CCodeBlock(codeblock_ofs, code_base, relo_base, id));
	codeblock_ofs += 8;
	id += 8;
    }
    cout << "Number of Code blocks = " << id/8 << endl;

    // die entry points
    int entry_base  = get_word(OFS_ENTRY_PTS);
    int entry_limit = get_word(OFS_CODE_BLOCKS);

    while(entry_base < entry_limit) {
	list<CCodeBlock*>::iterator i;
	i = code_list.begin();
	while(i!=code_list.end()) {
	    if((*i)->id == get_word(entry_base+4)) {
		entry_list.push_back(new CEntryBlock(entry_base, *i));
		break;
	    }
	    i++;
	}
	entry_base += 8;
    }
}

/*
 *  Neue Unit schreiben
 */
void write_new_file(char* name)
{
    int code_size = 0;                              // Gesamtgröße Code
    int codeblock_base = get_word(OFS_CODE_BLOCKS); // Position der Codeblocks
    list<CCodeBlock*>::iterator i;

    /* Größe der neuen Unit berechnen */
    /* unveränderte Teile */
    new_size = roundup(get_word(SYM_SIZE))
	+ roundup(get_word(CONST_SIZE))
	+ roundup(get_word(CONST_RELOC_SIZE))
        + browser_size();
    /* Code */
    for(i = code_list.begin(); i!=code_list.end(); i++) {
	if((**i).new_code) new_size += (**i).new_code->code_size;
	else new_size += (**i).code_size;
    }
    new_size = roundup(new_size);
    /* Relos */
    for(i = code_list.begin(); i!=code_list.end(); i++) {
	if((**i).new_code) new_size += (**i).new_code->relo_size;
	else new_size += (**i).relo_count*8;
    }
    new_size = roundup(new_size);
    new_unit = new char[new_size];
    char* code_ptr = new_unit;
    char* u_ptr = unit;

    /* Neue Unit erstellen*/
    /* Header */
    for(int m=roundup(get_word(SYM_SIZE))+browser_size(); m>0; m--)
	*code_ptr++ = *u_ptr++;

    /* Code */
    for(i=code_list.begin(); i!=code_list.end(); i++) {
	char* this_code;
	int this_len;
	if((**i).new_code) {
	    this_code = (**i).new_code->code;
	    this_len  = (**i).new_code->code_size;
	} else {
	    this_code = unit + (**i).code_ofs;
	    this_len  = (**i).code_size;
	}
	code_size += this_len;
	for(int j=0; j<this_len; j++)
	    *code_ptr++ = *this_code++;
	
	put_word(codeblock_base + (**i).id + 2, this_len);
    }

    int cs = code_size;  // Code aufrunden
    while(cs & 15) {
	*code_ptr++ = 0;
	cs++;
    }

    /* nun die CONST blocks verschieben */
    char* const_ptr = unit + roundup(get_word(SYM_SIZE))
	                   + roundup(get_word(CODE_SIZE))
                           + browser_size();

    for(int j=roundup(get_word(CONST_SIZE)); j>0; j--)
	*code_ptr++ = *const_ptr++;

    /* die Relos erzeugen */
    int relo_size = 0;
    for(i=code_list.begin(); i!=code_list.end(); i++) {
	char* this_relo;
	int this_len;
	if((**i).new_code) {
	    this_relo = (**i).new_code->relos;
	    this_len  = (**i).new_code->relo_size;
	} else {
	    this_relo = unit + (**i).relo_ofs;
	    this_len  = (**i).relo_count * 8;
	}
	relo_size += this_len;
	for(int j=0; j<this_len; j++)
	    *code_ptr++ = *this_relo++;
	
	put_word(codeblock_base + (**i).id + 4, this_len);
    }
    cs = relo_size;     // aufrunden
    while(cs & 15) {
	*code_ptr++ = 0;
	cs++;
    }

    /* die CONST relos kopieren */
    const_ptr = unit + roundup(get_word(SYM_SIZE))
	             + roundup(get_word(CODE_SIZE))
	             + roundup(get_word(CONST_SIZE))
	             + roundup(get_word(RELOC_SIZE))
                     + browser_size();

    for(int ii=roundup(get_word(CONST_RELOC_SIZE)); ii>0; ii--)
	*code_ptr++ = *const_ptr++;

    /* und die Daten in den Header eintragen */
    put_word(CODE_SIZE, code_size);
    put_word(RELOC_SIZE, relo_size);

    int n_size = code_ptr - new_unit;
    int n_size_1 = roundup(nget_word(SYM_SIZE))
	+ roundup(nget_word(CODE_SIZE)) + roundup(nget_word(CONST_SIZE))
	+ roundup(nget_word(RELOC_SIZE)) + roundup(nget_word(CONST_RELOC_SIZE))
        + browser_size();

    if(new_size != n_size) {
	cout << "Size mismatch error #1!" << endl;
	cout << "newsize = " << new_size << "  versus " << n_size << endl;
	return;
    }
    cout << "Length according to pointers: " << n_size << endl;
    cout << "Length according to header:   " << n_size_1 << "  (Code: " << code_size << ")" << endl;
    cout << "Original size:                " << unit_size << "  (Code: " << get_word(CODE_SIZE) << ")" << endl;
    if (browser_size())
        cout << "NOTE: " << browser_size() << " bytes of browser information present\n";
    if(n_size != n_size_1) {
	cout << "Size mismatch error!" << endl;
	return;
    }
    if(n_size > unit_size) {
	cout << "New code got *bigger*!" << endl;
	return;
    }
    cout << "Saved:                        " << unit_size - new_size
	 << "  (Code: " << get_word(CODE_SIZE) - code_size << ")" << endl;

    /* alles OK -> rausschreiben */
    FILE* fp = fopen(name, "wb");
    if(!fp) {
	cout << "Could not create output file." << endl;
	return;
    }
    fwrite(new_unit, 1, new_size, fp);
    fclose(fp);
}

/* Strukturen für Options-Parser */
struct TOption {
    char letter;
    char* long_name;
    bool* variable;
    char* desc;
};

TOption options[] = {
    { 'a', "data-flow-analysis", &do_dfa,          "data flow analysis" },
    { 'j', "reduce-jump-chains", &do_jumpchains,   "remove jump chains" },
    { 'p', "peephole-optimizations", &do_peephole, "replace insns by simpler ones" },
    { 'u', "remove-unused-code", &do_remunused,    "remove unreferenced code" },
    { 'd', "debug-dump", &do_dumps,                "produce debug dumps (files blockX.passY)" },
    { 'n', "names", &do_names,                     "read symbols and show function names" },
    { 's', "size", &do_size,                       "optimize for small size, not speed" },
    { 'e', "early-jump", &do_early_jmp,            "jump earlier to re-use identical code" },
    { 'l', "late-jump", &do_late_jmp,              "jump later to re-use identical code" },
    { 'r', "reg-alloc", &do_reg_alloc,             "basic register re-allocation" },
    { 'c', "combine-strings", &do_string_comb,     "combine common strings" },
    { 'm', "sort-moves", &do_sort_moves,           "sort `mov' insns" },
    { 'g', "cse", &do_the_cse,                     "common subexpression elimination" },
    { '3', "386", &do_386,                         "allow handling of some 386 insns" },
    { 0, 0, 0, 0 }
};

void help()
{
    cout << "Stefan's TPU Tuner - (c) copyright 1998,1999,2000,2002 by Stefan Reuther" << endl
	 << endl
	 << "Usage: tputuner [-options] file.in [file.out]" << endl
	 << endl
	 << "file.in is a TPU file (version " << UNIT_FORMAT << ", \"" UNIT_ID "\")" << endl
         << "file.out is the output file name (if different from file.in)" << endl
	 << endl
	 << "Options specify which optimizations to perform. Use an upper-case letter" << endl
	 << "(short options) or add `no-' (long options) to turn them off." << endl
	 << endl;
    for(TOption* p = options; p->letter!=0; p++) {
	cout << "-" << p->letter << "   --" << p->long_name;
	for(int i=strlen(p->long_name); i<25; i++) cout << " ";
	if(*(p->variable)) cout << "[on] "; else cout << "[off]";
	cout << "  " << p->desc << endl;
    }
    exit(0);
}

//
// Behandelt eine long-option
// p->Options-Wort (also "help", nicht "--help")
//
void handle_long_option(char* p)
{
    bool positive = true;
    if(p[0]=='n' && p[1]=='o' && p[2]=='-') {
	positive = false;
	p += 3;
    }
    if(strcmp(p, "help")==0) {
	if(!positive) {
	    cerr << "What do you mean with `no help'?" << endl;
	    exit(1);
	}
	help();
    }
    for(TOption* q = options; q->letter!=0; q++)
	if(strcmp(p, q->long_name)==0) {
	    *(q->variable) = positive;
	    return;
	}
    cerr << "tputuner: Invalid option: --" << p << endl;
    exit(1);
}

//
// Kurze Option behandeln
//
void handle_short_option(char c)
{
    for(TOption* q = options; q->letter!=0; q++) {
	if(q->letter == c) {
	    *(q->variable) = true;
	    return;
	}
	if(toupper(q->letter) == c) {
	    *(q->variable) = false;
	    return;
	}
    }
    cerr << "tputuner: Invalid option: -";
    if(c) cerr << c;
    cerr << endl;
    exit(1);
}

int main(int argc, char* argv[])
{
    char* infile = 0;
    char* outfile = 0;

    /* Options-Parser */
    for(int i = 1; i<argc; i++) {
	if(argv[i][0] == '-') {
	    char* p = argv[i]+1;
	    if(*p == '-') {
		/* long option */
		handle_long_option(p+1);
	    } else {
		/* short option */
		do handle_short_option(*p++); while(*p);
	    }
	} else {
	    if(infile==0) infile=argv[i];
	    else if(outfile==0) outfile=argv[i];
	    else {
		cerr << "tputuner: too many file names specified" << endl;
		exit(1);
	    }
	}
    }

    if(!infile) {
	cerr << "tputuner: You did not specify a file name" << endl
	     << "Type `tputuner --help' for help." << endl;
	exit(1);
    }
    if(!outfile) outfile=infile;

    if(do_early_jmp && !do_remunused) {
        do_early_jmp = false;
        cerr << "tputuner: early jump requires unused code removal" << endl;
    }
    if(do_late_jmp && !do_remunused) {
        do_late_jmp = false;
        cerr << "tputuner: late jump requires unused code removal" << endl;
    }
    if(do_sort_moves && !do_peephole) {
        do_sort_moves = false;
        cerr << "tputuner: move sorting requires peephole optimisation" << endl;
    }

    /* und action! */
    cout << "loading unit..." << endl;
    if(!load_unit(infile)) return 1;
    
    cout << "analyzing headers..." << endl;
    create_objects();
    if(do_names) read_hashtable(get_word(OFS_FULL_HASH), "");
    find_system_unit();

    cout << "optimizing..." << endl;
    for(list<CCodeBlock*>::iterator i=code_list.begin(); i!=code_list.end(); i++)
    {
	if((*i)->status == CCodeBlock::OK) {
            /* nicht mehr nötig, da calls stackframe-removal ausschalten */
/*            if((*i)->entry->flags & (INTERRUPT_PROC + CTOR_PROC + DTOR_PROC))
                  can_remove_stackframe = false;
              else
                  can_remove_stackframe = true;*/
            if((*i)->entry->name.length()) {
                cout << "Function " << (*i)->entry->name << ":" << endl;
            }
	    cout << "- code block #" << hex << (*i)->id << dec
                 << "... " << flush;
	    (*i)->optimize();
	}
    }

    if(do_string_comb) {
        cout << "string combination..." << endl;
        string_combine(code_list);
    }

    cout << "writing new file..." << endl;
    write_new_file(outfile);
}
