# SSA Form and Sparse Conditional Constant Propagation

`src/compiler/analysis/src/ssa.cpp`.

## SSA construction (Cytron et al.)

IR values are already single-assignment temporaries; SSA renames the scalar locals.
1. dominance frontiers with the Cooper-Harvey-Kennedy runner algorithm over the existing dominator
   tree;
2. phi placement at the iterated dominance frontier of every definition site (minimal SSA);
3. renaming by a walk over the dominator tree with one version stack per local.

```bash
./build/bin/nexusc analysis ssa tests/programs/constant_branches.nx
  bb3.if.cont:
    y.4 = phi [bb1.if.then: y.2] [bb2.if.else: y.3]
  bb4.while.cond:    ; DF = {bb4.while.cond}
    i.2 = phi [bb3.if.cont: i.1] [bb5.while.body: i.3]
```

## SCCP (Wegman and Zadeck)

Every SSA name lives in the lattice TOP > constant c > BOTTOM. Only CFG edges proven executable feed
phi nodes, so constants flow through branches whose conditions are themselves constant, and dead
branches are found at the same time.

```bash
./build/bin/nexusc analysis sccp tests/programs/constant_branches.nx
func main sccp: 7 computed value(s) constant, 1 branch(es) resolved, 1 unreachable block(s), 3 iteration(s)
  bb2.if.else   ; unreachable
  bb3.if.cont
    y.4 (phi) = 5
  bb4.while.cond
    i.2 (phi) = BOTTOM
```

`nexus_ssa_test` checks the phi placement (3 phis, for `y`, `i` and `sum` only), the single-assignment
property of every name, the resolved branch, the unreachable block and the lattice values.
