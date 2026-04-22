# OpenMP, MPI, SIMD, and GPU Notes

## CPU-First Benchmark Paths

Nexus keeps CPU-only benchmarking as the default validated path:

```bash
./build/bin/openmp-bench --kernel all --size 16 --threads 2
mpiexec --oversubscribe -n 2 ./build/bin/mpi-bench --kernel all --size 8
./build/bin/simd-bench --kernel all --size 32
./parallel-bench --all --build-dir ./build --repo-root .
```

## OpenMP

`openmp-bench` covers:

- vector add
- reduction
- tiny matmul
- branch-heavy integer work

The output is checksum-based, not timing-noisy, so tests remain deterministic.

## MPI

`mpi-bench` covers:

- reduction
- ring exchange

The intended process counts are small, typically 2 or 4:

```bash
mpiexec --oversubscribe -n 2 ./build/bin/mpi-bench --kernel all --size 8
```

## SIMD

`simd-bench` compares scalar and SIMD forms of:

- vector add
- dot product

The output includes scalar and SIMD checksums on the same line.

## Optional GPU

Phase 11 adds a tiny CUDA vector-add demo under `benchmarks/gpu_optional/`.

Default build:

```bash
cmake -G Ninja ..
```

Optional CUDA build:

```bash
cmake -G Ninja -DNEXUS_ENABLE_CUDA=ON ..
```

When CUDA is enabled and compiled successfully:

```bash
./build/bin/gpu-bench --kernel vector-add --size 64
```

If CUDA is disabled, the wrapper reports a deterministic skip:

```text
suite=gpu kernel=vector-add status=skipped reason=cuda-disabled
```

If CUDA is enabled but no compatible device is visible, `gpu-bench` still exits cleanly and reports:

```text
benchmark=gpu kernel=vector-add size=64 status=skipped reason=no-device
```
