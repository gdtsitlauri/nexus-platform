# Manycore, Networks-on-Chip and Asymmetric Cores

Extensions of `--mode parallel` (`src/sim/parallel/`).

## SPMD entry

A core without its own `coreN` label starts at `worker` with `$a0` = core id and `$a1` = number of
cores, so one program runs on any core count. `examples/parallel/spmd_sum.s` sums `8k .. 8k+7` on
core k, adds the partial sum to a shared total under the hardware lock, meets the others at the
barrier, and core 0 returns the total.

```bash
./build/bin/mips-sim run examples/parallel/spmd_sum.s --mode parallel --cores 64 --interconnect mesh --stats
Program exited with code 130816        # 0 + 1 + ... + 511
```

## Interconnects

| option | topology | hop cost |
| --- | --- | --- |
| `bus` | shared bus | 3 per message |
| `switch` | crossbar switch | 2 per message |
| `ring` | bidirectional ring | 1 + shortest ring distance |
| `mesh` | 2-D mesh, ceil(sqrt(cores)) wide, XY routing | 1 + Manhattan distance |
| `noc-lite` | fixed 2-wide mesh (original model) | 1 + Manhattan distance |

Coherence messages (invalidations, directory lookups) pay the hop cost of the chosen network.

## Asymmetric multiprocessing

`--core-cpi 1,1,4,4` makes cores 2 and 3 four times slower per instruction (big.LITTLE-style). In the
SPMD reduction on 4 cores the barrier then waits for the little cores (174 vs 71 cycles), with the
same result.

## Verification (`nexus_manycore`)

The reduction must be exact on 1, 2, 4, 8, 16, 32 and 64 cores over all five networks. An 8x8 mesh
must cost more hops per message than a 2x2 mesh, and little cores must slow the barrier without
changing the result.
