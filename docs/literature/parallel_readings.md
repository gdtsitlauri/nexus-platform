# Parallel Readings

These notes connect the bounded parallel-system layer in Nexus to the wider literature.

## 1. Lamport, *How to Make a Multiprocessor Computer That Correctly Executes Multiprocess Programs* (1979)

- Core point: defines sequential consistency in a programmer-facing way.
- Nexus link: `--consistency sc` is the baseline against which `weak-lite` is taught.

## 2. Nagarajan, Sorin, Hill, and Wood, *A Primer on Memory Consistency and Cache Coherence*

- Core point: coherence and consistency answer different questions.
- Nexus link: Phase 10 intentionally separates `--coherence` from `--consistency`.

## 3. MPI Forum, MPI standard documents

- Core point: MPI is a real communication model, not just a “hello rank” API.
- Nexus link: the tiny MPI benchmarks use real reduction and ring-exchange communication.

## 4. OpenMP specification material

- Core point: shared-memory parallelism depends on both worksharing and synchronization semantics.
- Nexus link: the OpenMP kernels exercise reductions, parallel loops, and deterministic small-thread execution.

## 5. Coherence-primer snooping and directory chapters

- Core point: snooping trades simplicity for scalability, while directory schemes reduce broadcast traffic.
- Nexus link: `snoop` and `directory-lite` are intentionally minimal versions of that contrast.

## Read-With-Code Suggestions

```bash
./build/bin/mips-sim run tests/golden/parallel_coherence_demo.s --mode parallel --coherence snoop --stats
./build/bin/mips-sim run tests/golden/parallel_coherence_demo.s --mode parallel --coherence directory-lite --stats
./build/bin/mips-sim run tests/golden/parallel_consistency_demo.s --mode parallel --consistency sc --stats
./build/bin/mips-sim run tests/golden/parallel_consistency_demo.s --mode parallel --consistency weak-lite --stats
```
