
EXENAME := tputuner
CFLAGS := -Wall -ansi -pedantic -O2 -g
GCC := gcc

# No user-serviceable parts below
OBJECTS := tputuner.o optimize.o insn.o disassemble.o codewriter.o assemble.o \
           dfa.o global.o peephole.o
LIBS := -lstdc++

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
dfa.o: insn.h codewriter.h dfa.h optimize.h global.h
disassemble.o: insn.h codewriter.h disassemble.h global.h
global.o: global.h
insn.o: insn.h codewriter.h assemble.h global.h
optimize.o: optimize.h insn.h codewriter.h disassemble.h dfa.h global.h
optimize.o: peephole.h
peephole.o: insn.h codewriter.h peephole.h dfa.h optimize.h global.h
tputuner.o: tpufmt.h optimize.h global.h
