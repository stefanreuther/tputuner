#ifndef TPU_FORMAT_H
#define TPU_FORMAT_H

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
    SYS_LONG_SQR = 0x50,        /* in: dx:ax=value  out: dx:ax=value^2 */
    SYS_LONG_MUL = 0x28,        /* in: dx:ax=a, cx:bx=b  out: dx:ax=a*b */

    CODE_PTR_REF = 0x30
};

#endif
