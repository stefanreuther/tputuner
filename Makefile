#
#  Makefile für TPUTUNER
#

EXENAME := tputuner
CFLAGS := -Wall -ansi -pedantic -O2 -g

ifdef DJDIR
GCC := gxx
else
GCC := g++
endif

# No user-serviceable parts below
OBJECTS := tputuner.o optimize.o insn.o disassemble.o codewriter.o assemble.o \
           dfa.o global.o peephole.o regalloc.o strcomb.o cse.o
LIBS :=

$(EXENAME): $(OBJECTS)
	$(GCC) $(CFLAGS) -o $(EXENAME) $(OBJECTS) $(LIBS)

.cc.o:
	$(GCC) $(CFLAGS) -o $*.o -c $*.cc

clean:
	rm $(OBJECTS)

depend:
	makedepend -- $(CFLAGS) -- *.cc
# DO NOT DELETE

assemble.o: assemble.h codewriter.h insn.h
codewriter.o: codewriter.h insn.h
cse.o: cse.h insn.h codewriter.h optimize.h global.h dfa.h
dfa.o: insn.h codewriter.h dfa.h optimize.h global.h
disassemble.o: insn.h codewriter.h disassemble.h global.h
global.o: global.h
insn.o: insn.h codewriter.h assemble.h global.h
optimize.o: optimize.h global.h insn.h codewriter.h disassemble.h dfa.h
optimize.o: peephole.h regalloc.h cse.h
peephole.o: insn.h codewriter.h peephole.h dfa.h optimize.h global.h tpufmt.h
peephole.o: cse.h
regalloc.o: regalloc.h insn.h codewriter.h optimize.h global.h
strcomb.o: strcomb.h global.h insn.h codewriter.h
tputuner.o: tpufmt.h optimize.h global.h strcomb.h
