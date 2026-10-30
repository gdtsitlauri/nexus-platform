# Glossary / Γλωσσάρι

The terms of the six courses in English and in Greek, as the Greek course outlines and lectures use them.
Learn each term together with its example in the guide rather than memorising the table.

## Computer operation, organisation and architecture

| English | Ελληνικά | meaning |
| --- | --- | --- |
| bit / byte / word | δυαδικό ψηφίο / ψηφιολέξη / λέξη | one binary digit / eight bits / four bytes in MIPS32 |
| register | καταχωρητής | named storage inside a CPU |
| address / value | διεύθυνση / τιμή | where data is stored / the data stored there |
| instruction set architecture, ISA | αρχιτεκτονική συνόλου εντολών | operations and operand formats visible to programs |
| assembly language | συμβολική γλώσσα (assembly) | instructions written with names instead of bit patterns |
| operand / immediate operand | όρισμα (τελούμενο) / άμεσο όρισμα | the value an instruction uses / a constant encoded in the instruction |
| accumulator / stack | συσσωρευτής / στοίβα | implicit operand register / last-in-first-out storage |
| general-purpose registers | καταχωρητές γενικού σκοπού | registers any instruction can name |
| procedure call / stack frame | κλήση υποπρογράμματος / πλαίσιο στοίβας | jump with return address / storage belonging to one call |
| program counter, PC | μετρητής προγράμματος | selects the next instruction |
| signed / unsigned integer | προσημασμένος / μη προσημασμένος ακέραιος | with or without a sign in the representation |
| two's complement | συμπλήρωμα ως προς 2 | the usual signed integer representation |
| fixed point / floating point | σταθερή / κινητή υποδιαστολή | fixed binary point / sign, exponent and fraction |
| adder / multiplier / divider | αθροιστής / πολλαπλασιαστής / διαιρέτης | the arithmetic circuits |
| carry lookahead | πρόβλεψη κρατουμένου | compute all carries in parallel |
| datapath | διαδρομή δεδομένων | connected units that read, compute and store values |
| control unit / micro-operation | μονάδα ελέγχου / μικρολειτουργία | selects operations / one register transfer of a step |
| hardwired / microprogrammed control | καλωδιωμένος / μικροπρογραμματισμένος έλεγχος | control as logic / control as a stored table |
| single-cycle / multi-cycle | απλού / πολλαπλών κύκλων μηχανής | one long cycle per instruction / one short cycle per step |
| pipelining | μερική επικάλυψη (διοχέτευση) | instructions overlap in different stages |
| hazard, dependence | κίνδυνος, εξάρτηση | an instruction needs something not ready yet |
| forwarding | παροχέτευση | bypass a result directly to its consumer |
| stall / flush | καθυστέρηση (φυσαλίδα) / απόρριψη | wait one cycle / discard wrong-path work |
| branch prediction | πρόβλεψη διακλαδώσεων | guess the next PC before the branch is resolved |
| latency / throughput | καθυστέρηση / ρυθμαπόδοση | time for one operation / operations completed per unit time |
| benchmark | μετροπρόγραμμα | a program used to measure performance |
| cache / locality | κρυφή μνήμη / τοπικότητα αναφορών | fast copies / reuse in time or nearby addresses |
| main / secondary memory | κύρια / βοηθητική μνήμη | DRAM / disk or flash |
| virtual memory / page / TLB | εικονική μνήμη / σελίδα / TLB | page-to-frame mapping / its unit / cache of translations |
| page fault | σφάλμα σελίδας | the page is not in memory and must be brought in |
| bus / interrupt / DMA | αρτηρία / διακοπή / άμεση προσπέλαση μνήμης | shared connection / device signal / device copies memory itself |
| superscalar | υπερβαθμωτός | issues several instructions per cycle |
| dynamic scheduling, out-of-order execution | δυναμική δρομολόγηση, εκτέλεση εκτός σειράς | ready independent instructions execute before older waiting ones |
| scoreboard / reservation stations | πίνακας παρακολούθησης / σταθμοί δέσμευσης | the two classic dynamic-scheduling structures |
| reorder buffer | ενταμιευτής αναδιάταξης | commits results in program order |
| static scheduling / VLIW | στατική δρομολόγηση / πολύ μεγάλη λέξη εντολής | the compiler packs independent operations |
| speculation | υποθετική εκτέλεση | execute before knowing the work is needed |
| predication | εκτέλεση υπό συνθήκη | guard an operation with a predicate |
| hardware description language, HDL | γλώσσα περιγραφής υλικού | Verilog or VHDL |

## Parallel systems

| English | Ελληνικά | meaning |
| --- | --- | --- |
| multithreading / simultaneous multithreading | πολλαπλά νήματα / ταυτόχρονα πολλαπλά νήματα | switch between threads / issue from several threads in one cycle |
| shared / distributed memory | κοινή / κατανεμημένη μνήμη | one address space / private memories and messages |
| symmetric / asymmetric multiprocessor | συμμετρικός / ασύμμετρος πολυεπεξεργαστής | identical / different cores |
| homogeneous / heterogeneous system | ομογενές / ετερογενές σύστημα | one kind / several kinds of processor |
| cache coherence | συνοχή κρυφής μνήμης | agreement on the value of one address |
| snooping / directory | υποκλοπή / κατάλογος | caches watch a bus / a table tracks the copies |
| memory consistency | συνέπεια μνήμης | ordering across memory operations |
| synchronisation / race / critical section | συγχρονισμός / συνθήκη ανταγωνισμού / κρίσιμο τμήμα | coordination / conflicting unsynchronised accesses / code protected as a unit |
| vector computer / SIMD | διανυσματικός υπολογιστής / SIMD | one instruction on many data elements |
| graphics processor, GPU | υπολογιστής (μονάδα) γραφικών | thousands of threads in groups of 32 |
| bus / switch / network on chip | αρτηρία / μεταγωγέας / δίκτυο σε ολοκληρωμένο | the communication architectures |

## Compilers

| English | Ελληνικά | meaning |
| --- | --- | --- |
| compiler | μεταγλωττιστής | translates a program into another language |
| grammar / production | γραμματική / κανόνας παραγωγής | rules generating valid syntax / one such rule |
| automaton / state machine | αυτόματο / μηχανή καταστάσεων | recogniser / remembered position in its computation |
| hash table / tree / graph | πίνακας κατακερματισμού / δέντρο / γράφημα | the data structures of a compiler |
| traversal / closure | διαπέραση / κλείσιμο | visit every node / everything reachable |
| token / lexeme | λεκτική μονάδα / λεξίδιο | recognised category / its actual spelling |
| lexical / syntax / semantic analysis | λεκτική / συντακτική / σημασιολογική ανάλυση | tokens / structure / meaning and types |
| syntax tree / abstract syntax tree | συντακτικό δέντρο / αφηρημένο συντακτικό δέντρο | full derivation / its essential structure |
| symbol table / scope | πίνακας συμβόλων / εμβέλεια | name bindings / region where a binding is visible |
| type checking / type system | έλεγχος τύπων / σύστημα τύπων | the rules on the types of expressions |
| attribute grammar | κατηγορική γραμματική (γραμματική ιδιοτήτων) | grammar with values and dependency rules attached to syntax |
| intermediate code / representation | ενδιάμεσος κώδικας / ενδιάμεση αναπαράσταση | form between source syntax and machine instructions |
| quadruples | τετράδες | `op, arg1, arg2, result` |
| code generation / instruction selection | παραγωγή τελικού κώδικα / επιλογή εντολών | machine code from IR / which instructions implement an operation |
| register allocation / spill | δέσμευση καταχωρητών / αποθήκευση στη μνήμη | map values to registers / keep a value in memory |
| optimisation | βελτιστοποίηση | faster code with the same result |
| generalised / parallel parsing | γενικευμένη / παράλληλη συντακτική ανάλυση | any context-free grammar / parts parsed at the same time |
| control-flow graph / basic block | γράφημα ροής ελέγχου / βασική ενότητα | possible transfers / straight-line operations |
| dominance / join point | κυριαρχία / σημείο σύγκλισης | every path passes through a block / where paths meet |
| lattice / fixed point | πλέγμα / σταθερό σημείο | ordered space of analysis facts / equations no longer change |
| data-flow analysis | ανάλυση ροής δεδομένων | propagate facts through the CFG (iteratively or by regions) |
| liveness | ζωντάνια μεταβλητών | a value may be needed later |
| symbolic analysis | συμβολική ανάλυση | values as formulas of other variables |
| loop unrolling | ξεδίπλωμα βρόχου (πραγματικό / συμβολικό) | copy loop bodies (known / unknown trip count) |
| iteration space / polyhedral transformation | χώρος επαναλήψεων / πολυεδρικός μετασχηματισμός | the points of a loop nest / a change of their order |
| linear programming | γραμμικός προγραμματισμός | optimise a linear goal under linear constraints |
| tiling | πλακόστρωση (tiling) | visit small iteration blocks for reuse |
| pointer / alias analysis | ανάλυση δεικτών / ψευδωνύμων | whether references can access the same object |
| interprocedural optimisation | διασυναρτησιακή βελτιστοποίηση | across function calls |

Back to the [study guides](README.md).
