# Compiler readings (NEY613, NEY709)

Classic papers behind the topics of the compiler guides. Read the paper, then run the matching example in the
guide section next to it.

| paper | topic | in NEXUS |
| --- | --- | --- |
| D. E. Knuth, *On the Translation of Languages from Left to Right*, Information and Control, 1965 | LR parsing | [NEY613 §3](../courses/NEY613.md#3-syntax-analysis-ll-and-lr-parsing) |
| F. DeRemer, *Simple LR(k) Grammars*, CACM, 1971 | SLR and LALR tables | [NEY613 §3](../courses/NEY613.md#3-syntax-analysis-ll-and-lr-parsing) |
| D. E. Knuth, *Semantics of Context-Free Languages*, Mathematical Systems Theory, 1968 | attribute grammars | [NEY613 §4](../courses/NEY613.md#4-semantic-analysis-and-attribute-grammars) |
| M. Poletto, V. Sarkar, *Linear Scan Register Allocation*, ACM TOPLAS, 1999 | register allocation | [NEY613 §6](../courses/NEY613.md#6-code-generation-instruction-selection-and-register-allocation) |
| J. Earley, *An Efficient Context-Free Parsing Algorithm*, CACM, 1970 | generalised parsing | [NEY709 §1](../courses/NEY709.md#1-front-end-extras-generalised-parsing-and-type-systems) |
| L. Damas, R. Milner, *Principal Type-Schemes for Functional Programs*, POPL, 1982 | type inference | [NEY709 §1](../courses/NEY709.md#1-front-end-extras-generalised-parsing-and-type-systems) |
| R. Cytron et al., *Efficiently Computing Static Single Assignment Form and the Control Dependence Graph*, ACM TOPLAS, 1991 | SSA, dominance frontiers | [NEY709 §2](../courses/NEY709.md#2-intermediate-representations-and-ssa) |
| G. A. Kildall, *A Unified Approach to Global Program Optimization*, POPL, 1973 | data-flow frameworks, lattices | [NEY709 §4](../courses/NEY709.md#4-data-flow-analysis-iterative-and-region-based) |
| F. E. Allen, J. Cocke, *A Program Data Flow Analysis Procedure*, CACM, 1976 | interval (region-based) analysis | [NEY709 §4](../courses/NEY709.md#4-data-flow-analysis-iterative-and-region-based) |
| M. N. Wegman, F. K. Zadeck, *Constant Propagation with Conditional Branches*, ACM TOPLAS, 1991 | SCCP | [NEY709 §5](../courses/NEY709.md#5-using-the-analyses-constants-and-symbolic-analysis) |
| L. Lamport, *The Parallel Execution of DO Loops*, CACM, 1974 | hyperplane (wavefront) schedules | [NEY709 §7](../courses/NEY709.md#7-iteration-spaces-and-polyhedral-transformations) |
| M. E. Wolf, M. S. Lam, *A Data Locality Optimizing Algorithm*, PLDI, 1991 | unimodular transformations, tiling | [NEY709 §7](../courses/NEY709.md#7-iteration-spaces-and-polyhedral-transformations) |
| P. Feautrier, *Some Efficient Solutions to the Affine Scheduling Problem*, Int. J. Parallel Programming, 1992 | schedules by linear programming | [NEY709 §7](../courses/NEY709.md#7-iteration-spaces-and-polyhedral-transformations) |
| L. O. Andersen, *Program Analysis and Specialization for the C Programming Language*, PhD thesis, 1994 | points-to analysis | [NEY709 §8](../courses/NEY709.md#8-pointers-calls-and-interprocedural-optimisation) |

Books: Aho, Lam, Sethi, Ullman, *Compilers: Principles, Techniques, and Tools* (2nd ed.; region-based analysis
is section 9.7); Allen, Kennedy, *Optimizing Compilers for Modern Architectures*.
