# Coherence

## Implemented Modes

Nexus implements two bounded coherence-lite modes in `src/sim/parallel/`:

- `snoop`
- `directory-lite`

Both modes share the same functional outcome for supported programs, but they account for events
differently and expose different educational control points.

## Cache State Model

The parallel simulator keeps a tiny per-core private cache map over word addresses with a bounded
state set:

- `Invalid`
- `Shared`
- `Modified`

The implementation is intentionally educational:

- writes update backing memory immediately
- stores invalidate peer copies when required
- reads can observe peer sharers and transition lines to `Shared`
- directory-lite mode tracks owner/sharer metadata separately from snooping-lite broadcasts

## CLI

```bash
./build/bin/mips-sim run tests/golden/parallel_coherence_demo.s --mode parallel --coherence snoop --stats
./build/bin/mips-sim run tests/golden/parallel_coherence_demo.s --mode parallel --coherence directory-lite --stats
```

## Reported Metrics

Parallel summaries now include:

- `Coherence`
- `Coherence events`
- `Invalidations`
- `Directory lookups` when `directory-lite` is selected
- `Interconnect messages`
- `Interconnect cycles`

## Validation Evidence

- `tests/unit/coherence_model_test.cpp`
- `tests/golden/parallel_coherence_demo.s`
- `tests/golden/parallel_golden_test.py`

The coherence demo is deliberately small: one core reads a shared location before another core
stores to it, forcing an invalidation and deterministic event accounting.

## Limitations

- no industrial MESI/MOESI implementation
- no multi-level private/shared cache hierarchy
- no coherence races beyond the bounded deterministic round-robin model
