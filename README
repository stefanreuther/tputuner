

           TPUTUNER - Ein Code-Optimierer for Turbo Pascal
       =======================================================


Funktion
---------

  TPUTUNER optimiert eine Turbo Pascal Unit (.tpu), um bessere
  Performance und vor allem auch geringere Codegröße zu erreichen. 
  Dazu wird die Unit disassembliert, optimiert, und wieder
  assembliert.

  TPUTUNER unterstützt Turbo Pascal 6.0 (bzw. TPW 1.5) und Borland
  Pascal 7.0; zwischen beiden Modi wird zur Compilierzeit
  unterschieden.


Aufruf
-------

  tputuner unit.tpu [ziel.tpu] [optionen]

  Die Zieldatei kann von der Unitdatei verschieden sein, um das
  Original zu erhalten. Folgende Optionen sind möglich:

  -a, --data-flow-analysis
  -j, --reduce-jump-chains
  -p, --peephole-optimizations
  -u, --remove-unused-code
  -e, --early-jump
  -l, --late-jump
  -r, --reg-alloc
  -m, --sort-moves
  -c, --combine-strings
  -g, --cse
       Legen die Art der Optimierung fest. Standardmäßig sind alle
       Optionen bis auf -r, -c und -g eingeschaltet. Um eine Option
       auszuschalten, verwenden Sie Großbuchstaben (also `-A')
       bzw. bei der langen Option `--no-' (also `--no-early-jump').
       Siehe auch die Beschreibung der einzelnen Optionen weiter
       unten.

  -d, --debug-dump
       Aktiviert die Erzeugung von Debug-Dumps, siehe unten.

  -n, --names
       Wenn aktiv (Standard) liest TPUTUNER die Symboltabellen aus um
       in den Statusmeldungen die Namen der bearbeiteten Funktionen
       anzeigen zu können. Wenn dies stört oder problematisch wird,
       kann es mit `-N' / `--no-names' abgeschaltet werden.

  -s, --size
       Optimiert auf Codegröße, nicht Geschwindigkeit. Damit werden
       teilweise Befehle erzeugt, die langsamer sind (i.a. mehr
       Speicherzugriffe), aber weniger Code benötigen.

  -3, --386
       Erlaubt die Nutzung von 386-Befehlen. Standardmäßig liest und
       schreibt TPUTUNER 286er-Code.


Statusmeldungen
----------------

  Während des Ablaufs werden Meldungen des folgenden Formates
  ausgegeben:

  - code block #248... 1! 2 3 r1  --> 47 bytes saved (new size = 1247)
                 \      \      \       \__ gesparter Code in diesem Block
                  \      \      \__ Reassembler-Passes
                   \      \__ Optimierer-Passes
                    \__ Codeblock-Nummer

  + Codeblock-Nummern: die erste implementierte Prozedur erhält Nummer
    0, dann folgt Nummer 8, 10, 18 (hex) usw., unabhängig von der
    Deklarations-Reihenfolge im INTERFACE-Teil.

  + Optimierer-Passes: `!' markieren Passes, in denen eine
    Code-Verbesserung auftrat. Nach 2 Passes mit identischem Ergebnis
    wird die Optimierung beendet.

  + Reassembler-Passes: meistens nur einer, mehrere können auftreten,
    wenn sich durch den ersten Pass noch Adressen verschoben haben, so
    daß Sprünge kürzer codiert werden konnten.


Fehlermeldungen
----------------

  Alle diese Meldungen führen dazu, daß der entsprechende Block nicht
  angefaßt wird. Damit soll Code erkannt werden, den TPUTUNER kaputt
  machen würde (I/O, Interrupt-Aufrufe, Daten im Codesegment, usw.)

  `Unrecognized opcode xx'
  `Direct interrupt access'
  `FPU is not supported' und ähnliche
    TPUTUNER kann nicht alle Befehle disassemblieren. Nicht erkannt
    werden alle 386+-Befehle, `test', Flag-Befehle, Port-I/O,
    String-Befehle.

  `Warning: code block contains bad relocation entries'
    Relozierungseinträge sind Stellen, an denen der Linker eine
    Adresse nachtragen muß. TPUTUNER alle Fälle behandeln, die Turbo
    Pascal erzeugt, einige Inline-Assembler-Konstruktionen müssen
    jedoch zurückgewiesen werden, z.B. `@foo: mov ax, offset @foo'.

  `Unrecognized relocation item at...'
    Relozierungen dürfen nur in 16-bit-Operanden auftreten. Eine
    Relozierung woanders ist ein sicheres Zeichen für Daten im
    Codesegment.


Inline-Assembler
-----------------

  ... wird fröhlich mit-optimiert. Dies ist erwünscht. TPUTUNER hätte
  sowieso keine Möglichkeit, Pascal- und Assembler-Code zu
  unterscheiden. Damit werden u.a. Sprünge gekürzt (`jmp finish'
  ... `finish: Exit;') und Speichertransfers eliminiert (`a :=
  Random(10); asm mov ax,a ...'). Kritische Assembler-Routinen outen
  sich hoffentlich durch nicht unterstützte Befehle im Code.

  Inline-Funktionen werden ebenfalls optimiert. Beispiel:
    function nachfolger(i:integer):integer; inline($58/$40);
    ...
    j:=nachfolger(3);
  wird von Turbo Pascal zu
     push 3
     pop  ax     ; $58
     inc  ax     ; $40
     mov  [j],ax
  TPUTUNER optimiert die push/pop-Kombination weg zu
     mov  ax,3

  Eingebundene .OBJ-Dateien werden i.A. nicht optimiert, da eine
  .OBJ-Datei von Turbo Pascal einen einzigen Code-Block mit mehreren
  Eintrittspunkten zugewiesen bekommt. In Assembler gelten auch die
  üblichen Sichtbarkeitsregeln nicht, so daß man fröhlich zwischen
  Prozeduren hin- und herspringen kann, was TPUTUNER nicht handhaben
  kann.


Optimierungen
--------------

* Datenfluß-Analyse (--data-flow-analysis)

  Eliminiert überflüssige Befehle, wenn die Ergebnisse, die sie
  erzeugen, bereits ermittelt wurden. Das sind vor allem Ladebefehle,
  aber auch Logikbefehle, wenn ein Operand 0 ist. Wahrscheinlich wäre
  der richtige Name `reload optimisation', aber `Datenfluß-Analyse'
  klingt einfach cooler ;-)

  + Beispiel:
        p^.a := 1;
        p^.b := 2;
    wird zu
        les(2)    di, [ss:bp+6]
        mov(2)    [es:di+0], 1
        les(2)    di, [ss:bp+6]
        mov(2)    [es:di+2], 2
    TPUTUNER streicht das zweite LES.

  + Fehlerbeispiel: Aliasing
        p := @a;
        a := b + 1;
        inc(p^);
        b := a + 1;
    wird zu:
        lea(2)    ax, [ss:bp+8]            ; p := @a
        mov(2)    dx, ss
        mov(2)    [ss:bp+fffffffc], ax
        mov(2)    [ss:bp+fffffffe], dx
        mov(2)    ax, [ss:bp+6]            ; a := b + 1
        inc(2)    ax
        mov(2)    [ss:bp+8], ax
        les(2)    di, [ss:bp+fffffffc]     ; inc(p^)
        inc(2)    [es:di+0]
        mov(2)    ax, [ss:bp+8]        **  ; b := a + 1
        inc(2)    ax
        mov(2)    [ss:bp+6], ax

    Dieser Code inkrementiert b dreimal. Der optimierte Code
    inkrementiert b nur zweimal, da die mit ** markierte Zeile als
    unnütz verworfen wird, "schließlich enthält AX bereits den Wert von
    [ss:bp+8]".

  + Fehlerbeispiel: Parameter in Registern
                mov     si,offset foo
                mov     di,si
                call    bar  { Quelle in SI, Ziel in DI }
    wird optimiert zu
                mov     di, offset foo
                call    bar
    TPUTUNER nimmt an, daß SI nur für die Initialisierung von DI
    geladen wurde, und ist sicher, alles über SI zu wissen.


* Sprungketten entfernen (--reduce-jump-chains)

  Wenn das Ziel eines Sprungs ein weiterer Sprungbefehl ist, wird
  direkt zum Endziel gesprungen. Das kommt häufig bei Konstruktionen wie
         IF foo THEN BEGIN
           IF bar THEN qux ELSE frob
         END ELSE fred;


* `Guckloch-Optimierungen' (--peephole-optimizations)

  Ersetzt komplexe Befehle durch einfachere. Das ist eine Art `Suchen
  und Ersetzen' und sollte mit TP-Code sicher sein. Hier werden die
  Befehle einzeln in einem Umfeld von nicht mehr als 2 oder 3 anderen
  Befehlen betrachtet, daher ``Guckloch''. Ein Beispiel ist hier
                cmp(2)  ax, 0
  wird zu
                or(2)   ax, ax
  egal, in welchem Kontext.

  Außerdem ersetzt diese Optimierung einige Aufrufe in die Unit SYSTEM
  durch Assembler-Instruktionen (siehe tpufmt.h), und modifiziert
  gelegentlich den Initialisierungscode einer FOR-Schleife.


* Unbenutzten Code entfernen (--remove-unused-code)

  Entfernt unreferenzierten Code, der unter anderem durch
  Sprungketten-Entfernung entsteht. Sollte immer sicher sein, außer
  bei Daten im Code...


* Sprünge vorziehen (--early-jump)

  Hier werden Sprünge eher durchgeführt, wenn am Ziel des Sprungs
  dieselben Befehle durchgeführt werden wie vor dem Sprung. Das tritt
  bei vielen Bedingungen zu, besonders wenn Strings oder Longints
  beteiligt sind. TPUTUNER erkennt zwei Blöcke auch dann als gleich,
  wenn sie sich in einem Befehl unterscheiden, den TPUTUNER
  korrigieren kann. Wenn diese Optimierung verwendet wird, darf die
  Entfernung unbenutzten Codes (`-U') nicht abgeschaltet werden.

  + Beispiel:
                cmp     [xxx], 1        IF xxx=1 THEN
                jne     skip
                push    10                func(10)
                call    func
                jmp     done            ELSE
        skip:   push    9                 func(9);
                call    func
        done:
    kann verkürzt werden zu
                cmp     [xxx], 1
                jne     skip
                push    10
                jmp     done
        skip:   push    9
        done:   call    func


* Sprünge verzögern (--late-jump)

  Diese Optimierung funktioniert ähnlich wie `--early-jump', hier
  werden jedoch Sprünge verzögert. Im Gegensatz zu `--early-jump' wird
  hier jedoch nicht immer Code gespart, der sich ergebende Code ist
  aber u.U. besser parallelisierbar.

  + Beispiel:
                cmp     [xxx],1         IF xxx=1 THEN
                jne     skip
                push    10                Fred(10)
                call    Fred
                jmp     done            ELSE
        skip:   push    10                Barney(10);
                call    Barney
        done:
    kann verkürzt werden zu
                cmp     [xxx],1
                push    10
                je      skip
                call    Fred
                jmp     done
        skip:   call    Barney
        done:


* Optimierung auf Größe (--size)

  Die meisten Optimierungen bringen sowohl einen Geschwindigkeits-
  gewinn, als auch reduzierte Programmgröße. Manche Änderungen sind
  jedoch mit einer Geschwindigkeitseinbuße verbunden. TPUTUNER wird
  diese nur dann durchführen, wenn Sie die --size-Option angegeben
  haben. Beispiel:
                mov     [foo], ss
                mov     es, [foo]
  wird ohne diese Option unverändert gelassen, mit --size wird diese
  Sequenz zu
                mov     [foo], ss
                push    ss
                pop     es
  was langsamer ist (mehr Speichertransfers), aber 2 Bytes kürzer. Den
  `mov [foo],ss' kann TPUTUNER nicht entfernen, da der Wert in [foo]
  evtl. später noch gebraucht wird.

  Allerdings kann auch ohne `-s' der Code langsamer werden, z.B. bei
  ungünstigen Konstellationen mit `--reg-alloc'.


* Register-Allokierung (--reg-alloc)

  Diese Optimierung veranlaßt TPUTUNER dazu, häufig benötigte Werte oder
  Adressen in Registern abzulegen. Das können Konstanten, lokale
  Variablen und Adressen sein. In kleinen Prozeduren ohne
  Unterprogrammaufrufe werden lokale Variablen u.U. komplett in
  Register gelegt.

  Diese Optimierung ist standardmäßig deaktiviert, da sie sehr
  zeitaufwendig ist. Da sie stellenweise Ergebnisse der DFA rückgängig
  macht (und dabei *immer* bessere lies kleinere Ergebnisse liefert),
  kann sie nur angewendet werden, wenn die DFA keine Änderungen mehr
  bringt.

  + Beispiel:
                var1 := 7;
                var2 := 7;
                var3 := 7;
    wird von Turbo Pascal zu
                mov     [var1],7
                mov     [var2],7
                mov     [var3],7
    und von TPUTUNER zu
                mov     cx,7
                mov     [var1],cx
                mov     [var2],cx
                mov     [var3],cx

    Letztere Sequenz ist kürzer (von der Codegröße her) und nicht
    unbedingt langsamer (da weniger Speichertransfers und in anderen
    Konstellationen als im Beispiel u.U. besser parallelisierbar).


* Ladebefehle sortieren (--sort-moves)

  Sortiert `mov'-Befehle. Dadurch können diese besser zu 16- und
  32-bit Operationen zusammengefasst werden. Allerdings werden damit
  Lock-Variablen ad absurdum geführt:
        lock := TRUE;
        critical1 := 1;
        critical2 := 42;
        lock := FALSE;
  sortiert, je nach Adreßlage, die beiden Zuweisungen an lock an den
  Anfang oder das Ende.

  Diese Optimierung ist Bestandteil der Peephole-Optimierung, also muß
  letztere eingeschaltet sein, um --sort-moves zu verwenden.


* Common Subexpression Elimination (--cse)

  Ungefähr das, was die CSE in "richtigen" Compilern macht: wiederholende
  Codeschnipsel weglassen. Wenn TPUTUNER eine Codefolge der Form
        <codeA>
        <codeB>
        <codeA>
  findet, wobei <codeB> das Ergebnis von <codeA> bestehen läßt, kann
  das zweite <codeA> weggelassen werden. Z.B. wird `p^.q^.x := p^.q^.y;'
  von Turbo Pascal zu
        les     di, [p]             \  codeA
        les     di, [es:di+10]      /
        mov     ax, [es:di]         >  codeB
        les     di, [p]             \
        les     di, [es:di+10]      /  codeA
        mov     [es:di+2], ax
  Die zweite Adreßrechnung kann weggelassen werden.

  Diese Optimierung ist standardmäßig ausgeschaltet, weil
  + sie langsam ist (die Suche nach gleichen Codestücken ist mindestens
    O(n^2))
  + hier Zeiger-Aliasing-Probleme besonders deutlich werden. TPUTUNER hat
    eine ultrasimple Aliasing-Erkennung für diese Optimierung (und gar
    keine Aliasing-Erkennung für den Rest!). Wie leicht man diese aushebeln
    kann, verbleibt zu ermitteln: wenn sowohl <codeA> als auch <codeB>
    auf eine Speicherzelle [es:di+10] schreiben, wird Aliasing angenommen;
    da Turbo Pascal keine Zeigerarithmetik kann, sollten Probleme relativ
    selten sein...


* String-Kombinierung (--combine-strings)

  (noch nicht fertig) Turbo Pascal faßt gleiche Stringkonstanten nur
  innerhalb einer Funktion zusammen. Diese Optimierung tut dies für
  eine komplette Unit. Allerdings muß sie dazu sehr konservativ
  vorgehen, um nicht andere Konstanten (float) zu zerstören.

  NOTE 10/Okt/2000: Vermutlich werde ich diese Optimierung jedoch
  wieder entfernen. Sie zu schreiben ist ziemlich bescheuert, und es
  bringt nicht soooo viel.


* Nutzung von 386er-Befehlen (--386)

  Erzeugt 386er-Befehle:
  + jcc j16
  + setcc r/m8
  + push i32, push m32
  + mov r/m32, i32

  Die 32-bit-Befehle tauchen in den Debuglistings nicht auf, sie
  werden als zwei 16-bit-Befehle mit derselben Adresse gelistet:
        98:  push(2)   [es:di+ffffffea]
        98:  push(2)   [es:di+ffffffe8]
  entspricht einem `push(4) [es:di+ffffffe8]'.

  TPUTUNER kann so bearbeitete Units nicht wieder einlesen!


* Was TPUTUNER nicht tut...

  + Vorausberechnung von Konstanten: das sollte der Turbo Pascal
    Compiler im Rahmen seiner Möglichkeiten getan haben.

  + Loop Unrolling (Schleifen aufrollen)

  + Function Inlining (Ideen für einen globalen(!) Inliner bestehen
    aber -- das kann nicht mal gcc ;-)

  + EXE-Dateien optimieren. Bei TPUs steht an den Konstanten noch
    dran, welche eine Adresse ist und welche nicht -- bei EXEn nicht.

  + Aliasing-Erkennung: TPUTUNER geht davon aus, daß kein Wert über
    zwei Zugriffspfade erreichbar ist, also z.B. über einen Zeiger und
    als feste Adresse. Quasi wird angenommen, alle Zeiger wären mit
    dem C-Modifier `restrict' definiert; keine Variable ist `volatile'
    (der Turbo-Pascal-Compiler nimmt genau das Gegenteil an).


Ergebnisse
-----------

  TPUTUNER kann die meisten Units um 2-10% verkürzen. Ich halte das
  für ein ganz ansehnliches Ergebnis.

  Beispiel-Ergebnisse:

  + PCC (ca. Version 0.99.5):
    - ca. 40 KLOC + 5 KLOC Bibliothekscode (nicht optimiert)
    - Normaler Turbo Pascal Code:
       cc.exe  112144
       cc.ovr  352649
             = 464793
    - Optimierung mit `tputuner -3sE', Version ca. 11/1998:
       cc.exe  110256
       cc.ovr  341475
             = 451731 (13062 = 2.8% gespart)

  + PCC (v1.0.6.3-debug):
    - ca. 70 KLOC
    - Normaler Turbo Pascal Code:
       cc.exe  244544
       cc.ovr  642800
             = 887344
    - Optimierung mit `tputuner -3rg', Version 10/2000:
       cc.exe  230672
       cc.ovr  583492
             = 814164 (73180 = 8.2% gespart)

  (Bei so großen interaktiven Programmen sollte man wohl vorrangig auf
  Größe optimieren -- Antwortzeiten sind eigentlich nie ein Problem)

  Die wichtigsten Optimierungen sind Datenflußanalyse (`-a') und
  Sprünge vorziehen (`-e') sowie evtl. CSE (`-g'). Die anderen
  Optimierungen alleine bringen nur marginale Verbesserungen. Alle
  zusammen angewendet können jedoch das Ergebnis verbessern: so kann
  die Sprungkettenverfolgung einige Sprünge unreferenziert machen, so
  daß sie beim Entfernen unbenutzten Codes Speicherplatz freigeben.


Optimierung unterstützen
-------------------------

  Wenn's auf jedes Byte ankommt...

  + Einfache Anweisungen / WITH: TPUTUNER verwaltet größtenteils nur
    eine Indirektionsebene. Die CSE kann mehr, erwischt aber auch nicht
    alles.
  + Zuweisungsketten / Wert direkt nach Ermittlung benutzen, damit
    Registerwerte wiederverwendet werden können.
  + Variablen zusammenfassen: Variablen, die oft gemeinsam genutzt
    werden, in den selben VAR-Block direkt beieinander packen (->
    mehrere Variablen können mit einem `mov' verwendet werden).
  + Kurze, häufig benutzte Variablen zuerst deklarieren, vor den
    großen (-> 8-bit Displacements).
  + Variablen, die häufig im Paar als Parameter übergeben werden,
    zusammen definieren: statische Variablen/Record-Elemente als
    `VAR y, x:INTEGER', lokale als `VAR x, y:INTEGER' (-> `foo(x,y)'
    kann diese als einen LONGINT übergeben bekommen).


Debuglistings
--------------

  Die Debuglistings (--debug-dump) haben die Dateinamen
  `blockX.passY' (`blockX.pY' unter DOS), wobei X die Blocknummer (hex)
  und Y die Pass-Nummer (dez) ist. Es werden nur die Optimizer-Passes
  protokolliert, nicht die Reassembler-Passes, da letztere effektiv
  nichts ändern.

  Das Format der Dumps ist ungefähr so:

  1a:  mov(2)    cx, 3
            \__ Operandgröße in Klammern, wichtig bei Befehlen ohne
                Register-Operanden
  1f:  callf     <un=13,rt=30,rb=30,ro=0>
          \                  \___ Verweis auf einen Relocation-Eintrag
           \___ `call far', im Gegensatz zu `call near'
  24:  mov(2)    [ss:bp+6], ax
  2e:  jcc`c'    <label:38>
         \            \_ Ziel = Label (derzeit an Offset 38hex)
          \_ Bedingter Sprung mit Condition Code (jcc`c' == jl, Opcode 7Ch),
             auch bei setcc (setcc`c' == setl, Opcode 0Fh 9Ch).
  38:  label`1'  <38>
   \          \_ Bei Labels: Referenzzähler (hex) = wieviel Sprünge
    \            verweisen auf dieses Label
     \_ Adresse (Hex)

  Manchmal erhalten Befehle einen zusätzlichen Operanden untergejubelt
  (z.B. `xor(2)  ax, ax, 0'), der für interne Abläufe in TPUTUNER
  wichtig ist.

  TPUTUNER erzeugt auch im Nicht-386er-Modus 386er-Befehle in den
  Debug-Listings. Diese werden in der Ausgabe vom Assembler wieder
  auseinandergenommen.

  Relocation-Einträge werden von TPUTUNER nicht weiter analysiert, bis
  auf die Annahme, daß der Wert `ro=' zur Berechnung des Abstandes im
  Speicher von zwei sonst identischen Einträgen benutzt werden kann,
  und das bestimmte Relocations Systemaufrufe der Unit SYSTEM bezeichnen
  (siehe tpufmt.h): long_mul, long_sqr und sload.


Compilieren
------------

  Ich compiliere TPUTUNER üblicherweise unter Linux mit gcc-2.8.1 oder
  neuer mit dem beliegenden Makefile. Unter DOS, mit DJGPP 2.02+
  compiliert TPUTUNER "out-of-the-box" mit GNU make.


Autor, Legalesisch
-------------------

  Stefan Reuther, <Streu@gmx.de>

  Kopieren dieses Programmes ist ausdrücklich erlaubt. Wenn Sie dieses
  Programm für mehr als bloß mal einen kurzen Test einsetzen, senden Sie
  doch bitte eine Mail mit Erfahrungsbericht oder ähnlichem.

  KEINE GARANTIE: DER EINSATZ VON tputuner ERFOLGT AUF EIGENE GEFAHR.
  FÜR DURCH tputuner ENTSTANDENE SCHÄDEN LEHNE ICH JEGLICHE HAFTUNG AB.
  WENN SIE SICH MIT tputuner EIN PROGRAMM KAPUTTMACHEN, MÜSSEN SIE FÜR
  DEN SCHADEN SELBST AUFKOMMEN. WENN SIE DIESES RISIKO NICHT AUF SICH
  NEHMEN WOLLEN, DÜRFEN SIE tputuner NICHT EINSETZEN.

  Ich verwende tputuner im täglichen Einsatz mit PCC (70000+ Zeilen),
  "kritische" Fehler fallen mir also meistens auf. Vielleicht verwenden
  Sie aber ander Code-Muster als ich, so das tputuner auf die Nase fällt.
  Oder der Fehler ist so subtil, daß er kaum auffällt. Ich hatte einmal
  einen Fall, daß in meiner grafischen Oberfläche die Beschriftungen der
  Buttons um 1 Pixel verschoben waren -- schuld war tputuner. Trotzdem
  scheint tputuner relativ stabil zu sein.


-eof-
