# Compiler Readings

These notes tie core compiler readings to the concrete Nexus implementation.

## 1. Aho, Sethi, and Ullman, *Compilers: Principles, Techniques, and Tools*

- Why it matters: classic reference for lexing, parsing, ASTs, semantics, and introductory optimization.
- Nexus link: the handwritten lexer/parser and bounded semantic analyzer follow the same “small real compiler” teaching style.

## 2. Cytron et al., *Efficiently Computing Static Single Assignment Form and the Control Dependence Graph* (1991)

- Why it matters: classic SSA reference.
- Nexus link: Nexus deliberately stops short of full SSA, but dominators and CFGs already prepare the ground for it.

## 3. Allen and Kennedy, *Optimizing Compilers for Modern Architectures*

- Why it matters: strong treatment of dependence analysis and loop transformations.
- Nexus link: Phase 9 implements only bounded unrolling, but the locality/polyhedral documentation points toward this line of work.

## 4. Appel, *Modern Compiler Implementation*

- Why it matters: practical bridge between compiler theory and real implementation.
- Nexus link: the typed three-address IR and explicit lowering passes fit this style well.

## 5. Muchnick, *Advanced Compiler Design and Implementation*

- Why it matters: still an excellent reference for register allocation, alias analysis, and interprocedural work.
- Nexus link: Phase 9’s symbolic, alias, and interprocedural analyses are intentionally much smaller, but this is the right expansion path.

## Read-With-Code Suggestions

```bash
./build/bin/nexusc ir examples/source_lang/factorial.nx
./build/bin/nexusc opt examples/source_lang/fixed_trip_unroll.nx --pass unroll
./build/bin/nexusc opt examples/source_lang/interproc_fold.nx --pass interproc-constfold
```
