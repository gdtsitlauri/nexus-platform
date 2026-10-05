# SIMT (GPU) Execution Model

`src/sim/simt/`, `mips-sim run <kernel> --mode simt --threads N --warp-size W`.

## Model

- `threads` threads run the kernel (label `kernel`, falling back to `main`) with `$a0` = thread id and
  `$a1` = thread count; each thread returns a value in `$v0`. NexusLang kernels are written as
  `fn kernel(tid: int, threads: int) -> int`.
- Warps of `warp-size` lanes issue one instruction per cycle in lock step, scheduled round-robin.
- **Divergence**: a SIMT reconvergence stack. At a divergent branch both paths run with partial
  masks, and the warp reconverges at the branch's immediate post-dominator (computed on the
  instruction-level CFG). When the reconvergence point equals the current entry's (loop exits),
  no extra continuation entry is pushed.
- **Memory**: addresses below the stack region are global and shared. Each thread's stack is private
  local memory, interleaved across threads exactly like CUDA local memory, so identical stack offsets
  from one warp coalesce. Each warp-level access is split into 128-byte transactions.

## Examples

| kernel | result |
| --- | --- |
| `examples/gpu/coalesced.s` (thread t touches word t) | 1 transaction per warp request |
| `examples/gpu/strided.s` (thread t touches word 32t) | 32 transactions per warp request |
| `examples/gpu/uniform.nx` | 100% SIMD efficiency, no divergence |
| `examples/gpu/collatz.nx` (odd/even branch and data-dependent loop trip counts) | 13.9% SIMD efficiency at warp size 32, 25.3% at 8 |

## Verification (`nexus_simt_test`)

- per-thread results equal a closed-form reference for every thread and both register-allocation
  modes, at warp size 32 and at warp size 1 (one thread per warp, i.e. scalar execution);
- coalescing counts (1 vs 32 transactions), divergence and SIMD efficiency as above;
- SIMT issue needs fewer issue slots than scalar execution.

## Limits

No thread blocks, shared memory, warp shuffles or atomics. One warp instruction per cycle with no
latency hiding model. Real-device CUDA is covered separately by the optional
`benchmarks/gpu_optional/` demo.
