# Phase 11 Parallel Benchmark Summary

Representative tiny benchmark lines:

```text
suite=openmp kernel=vector-add checksum=360 status=ok
suite=mpi kernel=reduce checksum=110 status=ok
suite=simd kernel=dot checksum=439 status=ok
suite=parallel case=coherence-demo cycles=9 status=ok
suite=gpu kernel=vector-add status=skipped reason=cuda-disabled
```

## Interpretation

- OpenMP, MPI, and SIMD are validated in tiny CPU-oriented configurations
- the parallel simulator contributes deterministic event/cycle summaries rather than wall-clock timings
- the optional GPU path is explicitly visible as either a successful tiny run or a clean skip
