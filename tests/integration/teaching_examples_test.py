#!/usr/bin/env python3
"""Run first lessons, then verify the changes students are asked to predict.

Core: teaching_examples_test.py <nexusc> <mips-sim> <repo>
Optional native examples: teaching_examples_test.py --native c|openmp|mpi <repo>
Native checks return 77 when the required compiler/runtime is unavailable.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run(args: list[str | Path], cwd: Path, expected: int = 0) -> str:
    env = os.environ.copy()
    env.setdefault("OMPI_ALLOW_RUN_AS_ROOT", "1")
    env.setdefault("OMPI_ALLOW_RUN_AS_ROOT_CONFIRM", "1")
    result = subprocess.run([str(a) for a in args], cwd=cwd, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=30, env=env)
    output = result.stdout + result.stderr
    require(result.returncode == expected,
            f"{' '.join(map(str, args))}: expected process exit {expected}, got {result.returncode}\n{output}")
    return output


def metric(output: str, label: str) -> int:
    match = re.search(rf"^{re.escape(label)}: (\d+)", output, re.M)
    require(match is not None, f"missing metric {label}\n{output}")
    return int(match.group(1))


def simulate(sim: Path, repo: Path, source: Path, value: int, mode: str = "pipeline", *options: str) -> str:
    output = run([sim, "run", source, "--mode", mode, "--stats", *options], repo)
    require(f"Program exited with code {value}\n" in output, f"wrong lesson result for {source}\n{output}")
    return output


def changed(source: Path, directory: Path, old: str, new: str) -> Path:
    text = source.read_text(encoding="utf-8")
    require(text.count(old) == 1, f"expected one editable fragment {old!r} in {source}")
    target = directory / source.name
    target.write_text(text.replace(old, new), encoding="utf-8")
    return target


def core(nexusc: Path, sim: Path, repo: Path) -> None:
    basics = repo / "examples/basics"
    expected = [12, 13, 1, 10, 9, 9, 10, 10, 7, 16, 0]
    sources = sorted((basics / "mips").glob("*.s"))
    require(len(sources) == len(expected), "update the expected result for each numbered lesson")
    for source, value in zip(sources, expected):
        for mode in ("functional", "single-cycle", "multi-cycle", "pipeline"):
            simulate(sim, repo, source, value, mode)

    with tempfile.TemporaryDirectory(prefix="nexus-lessons-") as tmp:
        directory = Path(tmp)
        edits = [
            ("01_add.s", "$t1, $zero, 7", "$t1, $zero, 9", 14),
            ("02_memory.s", "lw    $t1, 0($zero)", "lw    $t1, 4($zero)", 1),
            ("03_if.s", "$t0, $zero, 3", "$t0, $zero, 7", 0),
            ("04_sum.s", "$t1, $zero, 5", "$t1, $zero, 6", 15),
            ("05_call.s", "$a0, $zero, 8", "$a0, $zero, 10", 11),
        ]
        for name, old, new, value in edits:
            simulate(sim, repo, changed(basics / "mips" / name, directory, old, new), value)

        load = basics / "mips/08_load_use.s"
        require(metric(simulate(sim, repo, load, 10), "Load-use stalls") == 1, "missing load-use stall")
        moved = changed(load, directory, "  addu  $v0", "  addiu $t2, $zero, 1\n  addu  $v0")
        require(metric(simulate(sim, repo, moved, 10), "Load-use stalls") == 0,
                "independent instruction should fill the load-use slot")
        branch = basics / "mips/09_flush.s"
        wrong = simulate(sim, repo, branch, 7)
        require(metric(wrong, "Branch mispredictions") == 1 and metric(wrong, "Flushes") > 0,
                "flush lesson must discard wrong-path instructions")
        right = simulate(sim, repo, branch, 7, "pipeline", "--predictor", "static-taken")
        require(metric(right, "Branch mispredictions") == 0, "taken predictor should guess this branch")
        cache = basics / "mips/11_cache.s"
        require(metric(simulate(sim, repo, cache, 0, "pipeline", "--cache", "direct"), "Cache misses") == 2,
                "three accesses should first touch two cache lines")
        one_line = changed(cache, directory, "16($zero)", "8($zero)")
        require(metric(simulate(sim, repo, one_line, 0, "pipeline", "--cache", "direct"), "Cache misses") == 1,
                "changed accesses should touch one line")

        for scheduler in ("scoreboard", "tomasulo"):
            output = simulate(sim, repo, basics / "mips/10_scheduling.s", 16, "advanced",
                              "--scheduler", scheduler, "--trace")
            independent = output.index("ex(I3@pc3:addiu")
            consumer = output.index("ex(I2@pc2:mflo")
            require(independent < consumer, f"{scheduler} should execute independent I3 before waiting I2")

        def compile_run(source: Path, value: int) -> None:
            assembly = directory / (source.stem + ".s")
            run([nexusc, "compile", source, "-S", "-o", assembly], repo)
            for mode in ("functional", "single-cycle", "multi-cycle", "pipeline"):
                simulate(sim, repo, assembly, value, mode)

        for name, value in (("diamond.nx", 3), ("constant_branch.nx", 3), ("sum.nx", 10)):
            compile_run(basics / "compiler" / name, value)
        compile_run(repo / "examples/source_lang/tiny.nx", 14)
        compile_run(changed(repo / "examples/source_lang/tiny.nx", directory,
                            "2 + 3 * 4", "(2 + 3) * 4"), 20)
        compile_run(changed(basics / "compiler/diamond.nx", directory, "choose(true)", "choose(false)"), 4)
        compile_run(changed(basics / "compiler/sum.nx", directory, "sum(4)", "sum(5)"), 15)
        flow_source = repo / "examples/flow/sum.nx"
        for transformation in ("cse", "unroll"):
            for allocation in ("none", "linear-scan"):
                transformed = directory / f"flow-{transformation}-{allocation}.s"
                run([nexusc, "compile", flow_source, "-S", "--pass", transformation,
                     "--regalloc", allocation, "-o", transformed], repo)
                for mode in ("functional", "single-cycle", "multi-cycle", "pipeline"):
                    simulate(sim, repo, transformed, 36, mode)
                simulate(sim, repo, transformed, 36, "advanced", "--scheduler", "tomasulo")
        for options in (("--pass",), ("--pass", "unknown"), ("--pass", "cse", "--pass", "unroll")):
            run([nexusc, "compile", flow_source, "-S", *options], repo, expected=1)
        not_unrolled = directory / "unsupported.s"
        run([nexusc, "compile", repo / "examples/source_lang/tiny.nx", "-S", "--pass", "unroll",
             "-o", not_unrolled], repo, expected=2)
        require(not not_unrolled.exists(), "an unsupported unroll must not produce a misleading output file")
        ssa = run([nexusc, "analysis", "ssa", basics / "compiler/diamond.nx"], repo)
        require("func choose ssa: 1 phi node(s)" in ssa, "diamond should need one phi")
        regions = run([nexusc, "opt", basics / "compiler/diamond.nx", "--analysis", "regions"], repo)
        require("in {d1:y, d2:y}" in regions and "agrees" in regions,
                "reaching definitions at the diamond join should agree")
        sccp = run([nexusc, "analysis", "sccp", basics / "compiler/constant_branch.nx"], repo)
        require("1 branch(es) resolved, 1 unreachable block(s)" in sccp, "constant branch should be resolved")
        grammar = basics / "compiler/addition.g"
        output = run([nexusc, "grammar", grammar, "ll1", "2 + 3 + 4"], repo)
        require("accepted, synthesized value = 9" in output, "small grammar should evaluate three terms")
        # The grammar teaching command reports rejection in its output; the tool itself succeeds.
        rejected = run([nexusc, "grammar", grammar, "ll1", "2 +"], repo)
        require("rejected:" in rejected, "incomplete addition must be rejected")

    py = sys.executable
    adder = run([py, basics / "full_adder.py", "15", "1"], repo)
    require("low four bits: 0000 (0), carry out: 1" in adder, "carry demonstration failed")
    race = run([py, basics / "parallel/race.py"], repo)
    require("overlapping increments: 1" in race and "increments under a lock: 2" in race, "race demonstration failed")
    buffers = run([py, basics / "parallel/store_buffer.py"], repo)
    require("loads first: (0, 0)" in buffers and "before loads: (1, 1)" in buffers, "buffer demonstration failed")
    definitions = run([py, basics / "compiler/reaching_definitions.py"], repo)
    require("round 2: E=d0  T=d1  F=d2  J=d1,d2" in definitions, "manual data-flow rounds changed")
    print("teaching examples: 11 assembly lessons across 4 CPU models, compiler phases, analyses and predicted edits passed")


def native(kind: str, repo: Path) -> int:
    compiler = shutil.which("mpicc") if kind == "mpi" else next(
        (path for name in ("cc", "gcc", "clang") if (path := shutil.which(name))), None)
    launcher = (shutil.which("mpiexec") or shutil.which("mpirun")) if kind == "mpi" else None
    if compiler is None or (kind == "mpi" and launcher is None):
        print(f"SKIP {kind}: compiler/runtime unavailable")
        return 77
    source, expected = {
        "c": ("compiler/alias.c", "different objects: 1\nsame object: 2"),
        "openmp": ("parallel/sum_openmp.c", "sum=36"),
        "mpi": ("parallel/hello_mpi.c", "rank 0 received 42 from rank 1"),
    }[kind]
    with tempfile.TemporaryDirectory(prefix="nexus-native-lesson-") as tmp:
        executable = Path(tmp) / ("lesson.exe" if os.name == "nt" else "lesson")
        flags = ["-std=c11"] + (["-fopenmp"] if kind == "openmp" else [])
        # A found toolchain that cannot build a lesson is a failure, not a silent skip.
        run([compiler, *flags, repo / "examples/basics" / source, "-o", executable], repo)
        for count in ((1, 2, 4) if kind == "openmp" else (2,)):
            command = [launcher, "-n", "2", executable] if kind == "mpi" else [executable]
            if kind == "openmp":
                command.append(str(count))
            require(expected in run(command, repo), f"unexpected {kind} lesson output")
        if kind == "mpi":
            # Carry the shared computation from NexusLang/OpenMP into actual MPI processes.
            reduction = Path(tmp) / ("sum-mpi.exe" if os.name == "nt" else "sum-mpi")
            run([compiler, "-std=c11", repo / "examples/flow/sum_mpi.c", "-o", reduction], repo)
            for ranks in (1, 2, 4):
                require("sum=36" in run([launcher, "-n", str(ranks), reduction], repo),
                        "MPI flow must keep the same eight inputs and result for every rank count")
    print(f"native {kind} lesson: build and execution passed")
    return 0


def main() -> int:
    if len(sys.argv) == 4 and sys.argv[1] == "--native":
        require(sys.argv[2] in ("c", "openmp", "mpi"), "unknown native lesson kind")
        return native(sys.argv[2], Path(sys.argv[3]).resolve())
    require(len(sys.argv) == 4, __doc__ or "missing lesson arguments")
    binaries = [Path(arg).resolve() for arg in sys.argv[1:3]]
    core(*binaries, Path(sys.argv[3]).resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
