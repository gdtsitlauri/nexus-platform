# Polyhedral Loop-Nest Framework

`src/compiler/polyhedral/`, command `nexusc poly <file.loop> [transformations]`.

## Input

A perfect loop nest with inclusive affine bounds and affine subscripts:

```text
params N = 6, M = 7
for i = 1 to N {
  for j = 1 to M {
    A[i][j] = A[i - 1][j] + A[i][j - 1]
  }
}
```

## Dependence analysis

- iteration domains as integer polyhedra (`lower(x) <= x_k <= upper(x)`);
- for each pair of accesses to the same array with at least one write and for each carrying level,
  a dependence polyhedron over (source, target) iterations: both in the domain, equal subscripts, and
  the lexicographic order at that level;
- feasibility by **Fourier-Motzkin elimination with gcd tightening** (divide by the gcd of the
  coefficients and floor the bound), which proves, for example, that `A[2i]` and `A[2i+1]` never alias;
- the exact distance set by enumerating the concrete domain, from which direction vectors
  (`<`, `=`, `>`, `*`), uniform distances and parallel loops follow.

FM is a sound relaxation: `nexus_polyhedral_test` requires that it never misses a dependence that
enumeration finds, over 120 random nests.

## Transformations and code generation

Unimodular matrices for `interchange a b`, `reverse k` and `skew target source factor`, composed in
the given order. A transformation is legal when every carried distance vector stays lexicographically
positive after it. Loop bounds of the new nest come from Fourier-Motzkin projection, giving
`max(...)`/`min(...)` of `ceil`/`floor` expressions. Both nests are then executed and their arrays
compared.

```bash
./build/bin/nexusc poly examples/loops/wavefront.loop skew 2 1 1 interchange 1 2
  flow A: S1 -> S1  carried by loop i  direction (<,=)  distance (1,0)
  flow A: S1 -> S1  carried by loop j  direction (=,<)  distance (0,1)
parallel loops: none
legal: all transformed dependence distances are lexicographically positive
for c1 = 2 to 13 {
  for c2 = max(1, c1 - 7) to min(6, c1 - 1) {
    i = c2
    j = c1 - c2
    A[i][j] = A[i - 1][j] + A[i][j - 1]
  }
}
verification: 42 of 42 instances executed, arrays identical to the original nest
parallel loops after the transformation: c2
```

Examples: `wavefront.loop` (skewing exposes wavefront parallelism), `matmul.loop` (all six
permutations legal), `interchange_illegal.loop` (distance (1,-1) forbids interchange),
`triangular.loop` (non-rectangular domain, loop-independent dependence).

`nexus_polyhedral_test` also applies five transformations to 120 random nests (600 cases): every one
judged legal (341) reproduced the original results exactly.

## Limits

Perfect nests only; parameters take concrete values; FM is exact here thanks to gcd tightening and the
enumeration cross-check, but it is not a full integer-programming (Omega/ISL) solver. Tiling is not
generated as code.
