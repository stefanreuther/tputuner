/*
 *  tputuner - Ein Programm zur Optimierung von Turbo-Pascal Code
 *
 *  (c) copyright 1998,1999,2000,2002 by Stefan Reuther
 *
 *  Definitionen zum TPU-Format.
 *
 *  Siehe: INTRFC 7.0
 *    Public Domain by Milan Dadok.
 *    basiert auf einem Program von DJ Murdoch
 */
#ifndef TPU_FORMAT_H
#define TPU_FORMAT_H

#define UNIT_FORMAT 7

/* Flags für Codeblocks */
/* in ctor/dtor darf der Stackrahmen nicht entfernt werden! */
enum {
    FAR_PROC          = 1,
    INLINE_PROC       = 2,
    INTERRUPT_PROC    = 4,
    EXTERNAL_PROC     = 8,
    METHOD_PROC       = 16,
    CTOR_PROC         = 32,
    DTOR_PROC         = 64,
    ASM_PROC          = 128
};

#if UNIT_FORMAT == 6
#define UNIT_ID "TPU9"

/* Offsets im Unit-Header; head.pas */
enum {
    OFS_THIS_UNIT     = 0x08,
    OFS_HASHTABLE     = 0x0A,
    OFS_ENTRY_PTS     = 0x0C,
    OFS_CODE_BLOCKS   = 0x0E,
    OFS_CONST_BLOCKS  = 0x10,
    OFS_VAR_BLOCKS    = 0x12,
    OFS_DLL_LIST      = 0x14,
    OFS_UNIT_LIST     = 0x16,
    OFS_SRC_NAME      = 0x18,
    OFS_LINE_LENGTHS  = 0x1A,
    SYM_SIZE          = 0x1C,
    CODE_SIZE         = 0x1E,
    CONST_SIZE        = 0x20,
    RELOC_SIZE        = 0x22,
    CONST_RELOC_SIZE  = 0x24,
    VAR_SIZE          = 0x26,
    OFS_FULL_HASH     = 0x28,
    FLAGS             = 0x2A
};

/* Relocation Record:
    byte    Unit Nummer (Ziel)
    byte    typ              
               bit 7-6
	           00 = Code
		   01 = CS Const
		   10 = Var
		   11 = DS Const
	       bit 5-4
	           00 = relativ
		   01 = offset
		   10 = segment
		   11 = Zeiger
    word    Blocknummer (Ziel)
    word    Offset (Ziel)
    word    Offset, an dem relo eingetragen wird
*/

enum {
    /* Blocknummern */
    SYS_LONG_SQR = 0x50,        /* in: dx:ax=value  out: dx:ax=value^2 */
    SYS_LONG_MUL = 0x28,        /* in: dx:ax=a, cx:bx=b  out: dx:ax=a*b */
    SYS_LONG_DIV = 0x30,        /* in: dx:ax, cx:bx; out: quot/rem; clobbers si,di */
    SYS_SLOAD    = 0x58,        /* in: push dest, push src  out: src popped */

    /* Typen */
    CODE_PTR_REF = 0x30,        /* 4-byte pointer */
    CODE_OFS_REF = 0x50,        /* 2-byte offset */

    /* Object Types (nametype.pas, xxx_id) */
    ITYP_TYPE    = 81,
    ITYP_PROC    = 83,
    ITYP_UNIT    = 89,
    
    /* Unit block format (blocks.pas, unit_block_rec) */
    UNIT_BLOCK_NAME = 2,         /* offset of name */

    /* Type definition (nametype.pas, type_def_rec) */
    TYPE_HASH_OFS   = 6          /* offset of hash_ofs */
};
#elif UNIT_FORMAT == 7
#define UNIT_ID "TPUQ"

/* Offsets im Unit-Header; head.pas */
enum {
    OFS_THIS_UNIT     = 0x08,
    OFS_HASHTABLE     = 0x0A,
    OFS_ENTRY_PTS     = 0x0C,
    OFS_CODE_BLOCKS   = 0x0E,
    OFS_CONST_BLOCKS  = 0x10,
    OFS_VAR_BLOCKS    = 0x12,
    OFS_DLL_LIST      = 0x14,
    OFS_UNIT_LIST     = 0x16,
    OFS_SRC_NAME      = 0x18,
    OFS_LINE_LENGTHS  = 0x1C,     /* different from here */
    SYM_SIZE          = 0x1E,
    BROWSER_SIZE      = 0x20,     /* This one's new */
    CODE_SIZE         = 0x22,
    CONST_SIZE        = 0x24,
    RELOC_SIZE        = 0x26,
    CONST_RELOC_SIZE  = 0x28,
    VAR_SIZE          = 0x2A,
    OFS_FULL_HASH     = 0x2C,
    FLAGS             = 0x2E
};

/* Relocation Record:
    byte    Unit Nummer (Ziel)
    byte    typ              
               bit 7-6
	           00 = Code
		   01 = CS Const
		   10 = Var
		   11 = DS Const
	       bit 5-4
	           00 = relativ
		   01 = offset
		   10 = segment
		   11 = Zeiger
    word    Blocknummer (Ziel)
    word    Offset (Ziel)
    word    Offset, an dem relo eingetragen wird
*/

enum {
    /* Blocknummern */
    SYS_LONG_SQR = 1+0x50,      /* in: dx:ax=value  out: dx:ax=value^2 -- this one seems to no longer be in use. "+1" so it never matches */
    SYS_LONG_MUL = 0x28,        /* in: dx:ax=a, cx:bx=b  out: dx:ax=a*b */
    SYS_LONG_DIV = 0x30,        /* in: dx:ax, cx:bx; out: quot/rem; clobbers si,di */
    SYS_SLOAD    = 0x58,        /* in: push dest, push src  out: src popped */

    /* Typen */
    CODE_PTR_REF = 0x30,        /* 4-byte pointer */
    CODE_OFS_REF = 0x50,        /* 2-byte offset */

    /* Object Types (nametype.pas, xxx_id) */
    ITYP_TYPE    = 80,
    ITYP_PROC    = 82,
    ITYP_UNIT    = 83,

    /* Unit block format (blocks.pas, unit_block_rec) */
    UNIT_BLOCK_NAME = 4,         /* offset of name */

    /* Type definition (nametype.pas, type_def_rec) */
    TYPE_HASH_OFS   = 8          /* offset of hash_ofs */
};
#else
#  error "UNIT_FORMAT must be 6 or 7"
#endif

#endif
