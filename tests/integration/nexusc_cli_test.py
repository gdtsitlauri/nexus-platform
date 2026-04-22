#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=False)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    nexusc = Path(sys.argv[1])
    mips_sim = Path(sys.argv[2])
    repo = Path(sys.argv[3])
    examples = repo / "examples" / "source_lang"
    goldens = repo / "tests" / "golden"

    factorial = examples / "factorial.nx"
    arrays = examples / "arrays_and_loops.nx"
    fixed_trip = examples / "fixed_trip_unroll.nx"
    symbolic_trip = examples / "symbolic_unroll.nx"
    interproc_fold = examples / "interproc_fold.nx"
    bad_syntax = examples / "invalid_syntax.nx"
    bad_semantics = examples / "invalid_semantics.nx"

    help_result = run([str(nexusc), "--help"])
    require(help_result.returncode == 0, "help command failed")
    require("nexusc lex <file>" in help_result.stdout, "help output missing lex usage")
    require("nexusc ir <file>" in help_result.stdout, "help output missing ir usage")
    require("nexusc analysis liveness <file>" in help_result.stdout, "help output missing analysis usage")
    require(
        "nexusc experimental-parse <file> --mode parallel|bison-lr" in help_result.stdout,
        "help output missing experimental parse usage",
    )
    require(
        "nexusc opt <file> --analysis symbolic|region-liveness|affine|alias|interproc" in help_result.stdout,
        "help output missing Phase 9 analysis usage",
    )
    require(
        "nexusc opt <file> --pass unroll|unroll-symbolic|strip-mine|interproc-constfold" in help_result.stdout,
        "help output missing Phase 9 pass usage",
    )
    require("nexusc compile <file> -S [-o out.s]" in help_result.stdout, "help output missing compile usage")

    sim_help = run([str(mips_sim), "--help"])
    require(sim_help.returncode == 0, "mips-sim help command failed")
    require("mips-sim fp-demo <lhs> <rhs>" in sim_help.stdout, "mips-sim help missing fp-demo usage")
    require("--mode single-cycle" in sim_help.stdout, "mips-sim help missing single-cycle usage")
    require("--mode multi-cycle" in sim_help.stdout, "mips-sim help missing multi-cycle usage")
    require("--control hardwired|microcode" in sim_help.stdout, "mips-sim help missing control usage")
    require("--mode pipeline" in sim_help.stdout, "mips-sim help missing pipeline usage")
    require("--mode advanced" in sim_help.stdout, "mips-sim help missing advanced usage")
    require("--mode parallel" in sim_help.stdout, "mips-sim help missing parallel usage")
    require("--timeline" in sim_help.stdout, "mips-sim help missing timeline usage")
    require("--stats" in sim_help.stdout, "mips-sim help missing stats usage")
    require("--cache off|direct|assoc" in sim_help.stdout, "mips-sim help missing cache usage")
    require("--cache-l2 off|direct|assoc" in sim_help.stdout, "mips-sim help missing L2 cache usage")
    require("--l2-hit-latency N" in sim_help.stdout, "mips-sim help missing L2 latency usage")
    require("--io-demo" in sim_help.stdout, "mips-sim help missing io-demo usage")
    require("--interrupt-demo" in sim_help.stdout, "mips-sim help missing interrupt-demo usage")
    require("--dma-demo" in sim_help.stdout, "mips-sim help missing dma-demo usage")
    require(
        "--predictor static-not-taken|static-taken|static-btfnt" in sim_help.stdout,
        "mips-sim help missing predictor usage",
    )
    require("--predictor static-not-taken|2bit" in sim_help.stdout, "mips-sim help missing advanced predictor usage")
    require(
        "--scheduler inorder|vliw-lite|scoreboard" in sim_help.stdout,
        "mips-sim help missing advanced scheduler usage",
    )
    require("--issue-width 1|2" in sim_help.stdout, "mips-sim help missing issue-width usage")
    require("--coherence snoop|directory-lite" in sim_help.stdout, "mips-sim help missing coherence usage")
    require("--consistency sc|weak-lite" in sim_help.stdout, "mips-sim help missing consistency usage")
    require("--interconnect bus|switch|noc-lite" in sim_help.stdout, "mips-sim help missing interconnect usage")
    require("--cores N" in sim_help.stdout, "mips-sim help missing cores usage")

    fp_demo = run([str(mips_sim), "fp-demo", "1.5", "0.25"])
    require(fp_demo.returncode == 0, "fp-demo command failed")
    require("fp-demo f32:" in fp_demo.stdout, "fp-demo output missing header")
    require("compare: greater" in fp_demo.stdout, "fp-demo output missing comparison")
    require("fixed-q8.8:" in fp_demo.stdout, "fp-demo output missing fixed-point section")

    lex_result = run([str(nexusc), "lex", str(factorial)])
    require(lex_result.returncode == 0, "lex command failed on valid file")
    require("fn" in lex_result.stdout and "identifier" in lex_result.stdout, "lex output missing tokens")

    parse_result = run([str(nexusc), "parse", str(factorial)])
    require(parse_result.returncode == 0, "parse command failed on valid file")
    require("Parse succeeded: 2 function(s)" in parse_result.stdout, "parse success output mismatch")

    ast_result = run([str(nexusc), "ast", str(factorial)])
    require(ast_result.returncode == 0, "ast command failed on valid file")
    require("Function factorial" in ast_result.stdout, "ast output missing function")
    require("Call" in ast_result.stdout, "ast output missing call node")

    check_result = run([str(nexusc), "check", str(factorial)])
    require(check_result.returncode == 0, "check command failed on valid file")
    require(
        "Semantic check succeeded: 2 function(s) validated" in check_result.stdout,
        "semantic success output mismatch",
    )

    ir_result = run([str(nexusc), "ir", str(factorial)])
    require(ir_result.returncode == 0, "ir command failed on valid file")
    require("func factorial(n: int) -> int {" in ir_result.stdout, "ir output missing function header")
    require("call factorial(" in ir_result.stdout, "ir output missing recursive call")

    cfg_result = run([str(nexusc), "cfg", str(arrays)])
    require(cfg_result.returncode == 0, "cfg command failed on valid file")
    require("func sum4 cfg:" in cfg_result.stdout, "cfg output missing function header")
    require(
        "succs: bb2.while.body, bb3.while.cont" in cfg_result.stdout,
        "cfg output missing branch successors",
    )

    dom_result = run([str(nexusc), "dom", str(arrays)])
    require(dom_result.returncode == 0, "dom command failed on valid file")
    require("func sum4 dominators:" in dom_result.stdout, "dom output missing function header")
    require("idom = bb1.while.cond" in dom_result.stdout, "dom output missing loop header idom")

    analysis_result = run([str(nexusc), "analysis", "liveness", str(arrays)])
    require(analysis_result.returncode == 0, "analysis command failed on valid file")
    require("func sum4 liveness:" in analysis_result.stdout, "analysis output missing function header")
    require(
        "{values: int[4], i: int, total: int}" in analysis_result.stdout,
        "analysis output missing expected live local set",
    )

    experimental_parse = run([str(nexusc), "experimental-parse", str(factorial), "--mode", "parallel"])
    require(experimental_parse.returncode == 0, "experimental parse command failed on valid file")
    require(
        "experimental parallel parse partitions:" in experimental_parse.stdout,
        "experimental parse output missing partition header",
    )
    require(
        "Experimental parallel parse succeeded: 2 function(s)" in experimental_parse.stdout,
        "experimental parse output missing success summary",
    )

    bison_parse = run([str(nexusc), "experimental-parse", str(factorial), "--mode", "bison-lr"])
    require(bison_parse.returncode == 0, "bison-lr experimental parse command failed on valid file")
    require(
        "experimental bison-lr parse summary:" in bison_parse.stdout,
        "bison-lr parse output missing summary header",
    )
    require(
        "Experimental bison-lr parse succeeded: 2 function(s)" in bison_parse.stdout,
        "bison-lr parse output missing success summary",
    )

    symbolic_result = run([str(nexusc), "opt", str(arrays), "--analysis", "symbolic"])
    require(symbolic_result.returncode == 0, "symbolic opt analysis failed")
    require("func sum4 symbolic:" in symbolic_result.stdout, "symbolic analysis output missing function header")

    region_result = run([str(nexusc), "opt", str(arrays), "--analysis", "region-liveness"])
    require(region_result.returncode == 0, "region-liveness opt analysis failed")
    require(
        "func sum4 region-liveness:" in region_result.stdout,
        "region-liveness output missing function header",
    )
    require(
        "(loop) {bb1.while.cond, bb2.while.body}" in region_result.stdout,
        "region-liveness output missing loop region",
    )

    affine_result = run([str(nexusc), "opt", str(arrays), "--analysis", "affine"])
    require(affine_result.returncode == 0, "affine opt analysis failed")
    require("func sum4 affine:" in affine_result.stdout, "affine analysis output missing function header")
    require("memory-locals: values" in affine_result.stdout, "affine analysis missing memory-local summary")

    alias_result = run([str(nexusc), "opt", str(arrays), "--analysis", "alias"])
    require(alias_result.returncode == 0, "alias opt analysis failed")
    require("func sum4 alias:" in alias_result.stdout, "alias analysis output missing function header")
    require("pairs:" in alias_result.stdout, "alias analysis output missing pair summary")

    interproc_result = run([str(nexusc), "opt", str(interproc_fold), "--analysis", "interproc"])
    require(interproc_result.returncode == 0, "interprocedural opt analysis failed")
    require(
        "interprocedural summaries:" in interproc_result.stdout,
        "interprocedural analysis output missing summary header",
    )
    require("func inc: pure=yes, tiny=yes" in interproc_result.stdout, "interproc output missing pure callee")

    concrete_unroll = run([str(nexusc), "opt", str(fixed_trip), "--pass", "unroll"])
    require(concrete_unroll.returncode == 0, "concrete unroll pass failed")
    require("while.unrolled.0" in concrete_unroll.stdout, "concrete unroll output missing first clone")
    require(
        "concrete unroll: fully unrolled loop with trip count 4" in concrete_unroll.stdout,
        "concrete unroll output missing transformation note",
    )

    symbolic_unroll = run([str(nexusc), "opt", str(symbolic_trip), "--pass", "unroll-symbolic"])
    require(symbolic_unroll.returncode == 0, "symbolic unroll pass failed")
    require("while.unroll.check" in symbolic_unroll.stdout, "symbolic unroll output missing guard block")
    require(
        "symbolic unroll: applied factor-2 unrolling with residual guard" in symbolic_unroll.stdout,
        "symbolic unroll output missing transformation note",
    )

    strip_mine_result = run([str(nexusc), "opt", str(fixed_trip), "--pass", "strip-mine"])
    require(strip_mine_result.returncode == 0, "strip-mine pass failed")
    require("while.strip.guard" in strip_mine_result.stdout, "strip-mine output missing guard block")
    require(
        "affine strip-mine: applied tile factor 2" in strip_mine_result.stdout,
        "strip-mine output missing transformation note",
    )

    interproc_fold_result = run([str(nexusc), "opt", str(interproc_fold), "--pass", "interproc-constfold"])
    require(interproc_fold_result.returncode == 0, "interproc const-fold pass failed")
    require("const_int 5" in interproc_fold_result.stdout, "interproc const-fold missing first folded value")
    require("const_int 3" in interproc_fold_result.stdout, "interproc const-fold missing second folded value")
    require(
        "interprocedural constant fold replaced 2 call(s)" in interproc_fold_result.stdout,
        "interproc const-fold missing replacement summary",
    )

    compile_ir_result = run([str(nexusc), "compile", str(factorial), "--emit-ir"])
    require(compile_ir_result.returncode == 0, "compile --emit-ir failed")
    require("func factorial(n: int) -> int {" in compile_ir_result.stdout, "compile --emit-ir output mismatch")

    with tempfile.TemporaryDirectory() as temp_dir:
        asm_path = Path(temp_dir) / "factorial.s"
        compile_result = run([str(nexusc), "compile", str(factorial), "-S", "-o", str(asm_path)])
        require(compile_result.returncode == 0, "compile -S failed on valid file")
        require(asm_path.exists(), "compile did not create assembly output file")
        assembly_text = asm_path.read_text()
        require("factorial:" in assembly_text, "assembly output missing function label")
        require("jal factorial" in assembly_text, "assembly output missing recursive call")

        functional = run([str(mips_sim), "run", str(asm_path), "--mode", "functional"])
        require(functional.returncode == 0, "functional simulator failed")
        require("Mode: functional" in functional.stdout, "functional output missing mode summary")
        require("Program exited with code 120" in functional.stdout, "functional execution result mismatch")

        single_cycle = run([str(mips_sim), "run", str(asm_path), "--mode", "single-cycle"])
        require(single_cycle.returncode == 0, "single-cycle simulator failed")
        require("Mode: single-cycle" in single_cycle.stdout, "single-cycle output missing mode summary")
        require("Program exited with code 120" in single_cycle.stdout, "single-cycle execution result mismatch")

        multi_cycle = run([str(mips_sim), "run", str(asm_path), "--mode", "multi-cycle"])
        require(multi_cycle.returncode == 0, "multi-cycle simulator failed")
        require("Mode: multi-cycle" in multi_cycle.stdout, "multi-cycle output missing mode summary")
        require("Control: hardwired" in multi_cycle.stdout, "multi-cycle output missing control summary")
        require("Program exited with code 120" in multi_cycle.stdout, "multi-cycle execution result mismatch")

        pipeline_off = run([str(mips_sim), "run", str(asm_path), "--mode", "pipeline", "--stats"])
        require(pipeline_off.returncode == 0, "pipeline simulator failed")
        require("Mode: pipeline" in pipeline_off.stdout, "pipeline output missing mode summary")
        require("Program exited with code 120" in pipeline_off.stdout, "pipeline execution result mismatch")

        advanced = run(
            [
                str(mips_sim),
                "run",
                str(asm_path),
                "--mode",
                "advanced",
                "--predictor",
                "2bit",
                "--issue-width",
                "2",
                "--stats",
            ]
        )
        require(advanced.returncode == 0, "advanced simulator failed")
        require("Mode: advanced" in advanced.stdout, "advanced output missing mode summary")
        require("Predictor: 2bit" in advanced.stdout, "advanced output missing predictor summary")
        require("Issue width: 2" in advanced.stdout, "advanced output missing issue-width summary")
        require("Program exited with code 120" in advanced.stdout, "advanced execution result mismatch")

        parallel_single = run(
            [str(mips_sim), "run", str(asm_path), "--mode", "parallel", "--cores", "1", "--stats"]
        )
        require(parallel_single.returncode == 0, "parallel simulator failed on single-core compiled program")
        require("Mode: parallel" in parallel_single.stdout, "parallel output missing mode summary")
        require("Cores: 1" in parallel_single.stdout, "parallel output missing core summary")
        require("Program exited with code 120" in parallel_single.stdout, "parallel execution result mismatch")

        pipeline_trace = run(
            [str(mips_sim), "run", str(goldens / "trace_demo.s"), "--mode", "pipeline", "--trace"]
        )
        require(pipeline_trace.returncode == 0, "pipeline trace execution failed")
        require("trace[pipeline]: cycle=1 IF=I0@pc0:addiu" in pipeline_trace.stdout, "pipeline trace missing trace line")
        require("Program exited with code 7" in pipeline_trace.stdout, "pipeline trace missing final result")

        advanced_trace = run(
            [
                str(mips_sim),
                "run",
                str(goldens / "advanced_vliw_demo.s"),
                "--mode",
                "advanced",
                "--scheduler",
                "vliw-lite",
                "--issue-width",
                "2",
                "--trace",
            ]
        )
        require(advanced_trace.returncode == 0, "advanced trace execution failed")
        require(
            "trace[advanced]: cycle=1 scheduler=vliw-lite issue_width=2 packet=[I0@pc0:addiu, I2@pc2:addiu] events=-"
            in advanced_trace.stdout,
            "advanced trace missing VLIW bundle",
        )
        require("Program exited with code 6" in advanced_trace.stdout, "advanced trace missing final result")

        scoreboard_trace = run(
            [
                str(mips_sim),
                "run",
                str(goldens / "advanced_vliw_demo.s"),
                "--mode",
                "advanced",
                "--scheduler",
                "scoreboard",
                "--issue-width",
                "1",
                "--trace",
            ]
        )
        require(scoreboard_trace.returncode == 0, "advanced scoreboard execution failed")
        require("Scheduler: scoreboard" in scoreboard_trace.stdout, "advanced scoreboard output missing scheduler summary")
        require(
            "trace[advanced]: cycle=1 scheduler=scoreboard" in scoreboard_trace.stdout,
            "advanced scoreboard trace missing first cycle",
        )

        predictor_compare = run(
            [
                str(mips_sim),
                "run",
                str(goldens / "advanced_branch_demo.s"),
                "--mode",
                "advanced",
                "--predictor",
                "2bit",
                "--stats",
            ]
        )
        require(predictor_compare.returncode == 0, "advanced predictor execution failed")
        require("Branch mispredictions: 2" in predictor_compare.stdout, "advanced predictor stats mismatch")

        coherence_demo = run(
            [
                str(mips_sim),
                "run",
                str(goldens / "parallel_coherence_demo.s"),
                "--mode",
                "parallel",
                "--cores",
                "2",
                "--coherence",
                "snoop",
                "--stats",
            ]
        )
        require(coherence_demo.returncode == 0, "parallel coherence demo failed")
        require("Coherence: snoop" in coherence_demo.stdout, "parallel coherence demo missing coherence summary")
        require("Program exited with code 7" in coherence_demo.stdout, "parallel coherence demo result mismatch")

        consistency_demo = run(
            [
                str(mips_sim),
                "run",
                str(goldens / "parallel_consistency_demo.s"),
                "--mode",
                "parallel",
                "--cores",
                "2",
                "--consistency",
                "weak-lite",
                "--stats",
            ]
        )
        require(consistency_demo.returncode == 0, "parallel consistency demo failed")
        require("Consistency: weak-lite" in consistency_demo.stdout, "parallel consistency demo missing model")
        require("Store-buffer flushes:" in consistency_demo.stdout, "parallel consistency demo missing flush stats")

        pipeline_timeline = run(
            [str(mips_sim), "run", str(goldens / "pipeline_branch_demo.s"), "--mode", "pipeline", "--timeline"]
        )
        require(pipeline_timeline.returncode == 0, "pipeline timeline execution failed")
        require("timeline[pipeline]:" in pipeline_timeline.stdout, "pipeline timeline missing header")
        require("ID!" in pipeline_timeline.stdout, "pipeline timeline missing flush marker")
        require("Branch mispredictions: 1" in pipeline_timeline.stdout, "pipeline timeline missing branch stats")

        io_demo = run(
            [str(mips_sim), "run", str(goldens / "io_demo.s"), "--mode", "pipeline", "--io-demo", "--stats"]
        )
        require(io_demo.returncode == 0, "pipeline I/O demo failed")
        require("io[console]: value=65 char='A'" in io_demo.stdout, "I/O demo missing console output")
        require("Program exited with code 65" in io_demo.stdout, "I/O demo result mismatch")

        interrupt_demo = run(
            [
                str(mips_sim),
                "run",
                str(goldens / "interrupt_demo.s"),
                "--mode",
                "pipeline",
                "--interrupt-demo",
                "--stats",
            ]
        )
        require(interrupt_demo.returncode == 0, "pipeline interrupt demo failed")
        require("Program exited with code 77" in interrupt_demo.stdout, "interrupt demo result mismatch")
        require("Interrupts handled: 1" in interrupt_demo.stdout, "interrupt demo missing interrupt stats")

        dma_demo = run(
            [str(mips_sim), "run", str(goldens / "dma_demo.s"), "--mode", "pipeline", "--dma-demo", "--stats"]
        )
        require(dma_demo.returncode == 0, "pipeline DMA demo failed")
        require("Program exited with code 21" in dma_demo.stdout, "DMA demo result mismatch")
        require("DMA words copied: 2" in dma_demo.stdout, "DMA demo missing DMA stats")

        array_path = Path(temp_dir) / "arrays.s"
        compile_arrays = run([str(nexusc), "compile", str(arrays), "-S", "-o", str(array_path)])
        require(compile_arrays.returncode == 0, "compile failed on arrays example")
        arrays_pipeline = run(
            [str(mips_sim), "run", str(array_path), "--mode", "pipeline", "--cache", "assoc", "--ways", "2", "--stats"]
        )
        require(arrays_pipeline.returncode == 0, "pipeline simulator failed on arrays example")
        require("Program exited with code 10" in arrays_pipeline.stdout, "arrays pipeline result mismatch")
        require("Cache: 2-way set-associative" in arrays_pipeline.stdout, "assoc cache summary missing")
        require("Cache hits:" in arrays_pipeline.stdout, "assoc cache run missing cache-hit stats")
        require("Cache misses:" in arrays_pipeline.stdout, "assoc cache run missing cache-miss stats")

        arrays_pipeline_l2 = run(
            [
                str(mips_sim),
                "run",
                str(array_path),
                "--mode",
                "pipeline",
                "--cache",
                "direct",
                "--sets",
                "1",
                "--line-words",
                "1",
                "--cache-l2",
                "assoc",
                "--l2-ways",
                "2",
                "--l2-sets",
                "2",
                "--l2-line-words",
                "1",
                "--stats",
            ]
        )
        require(arrays_pipeline_l2.returncode == 0, "pipeline simulator failed on arrays example with L2")
        require(
            "Cache: direct-mapped + L2 2-way set-associative" in arrays_pipeline_l2.stdout,
            "L2 cache summary missing",
        )
        require("L2 hits:" in arrays_pipeline_l2.stdout, "L2 cache run missing L2-hit stats")
        require("L2 misses:" in arrays_pipeline_l2.stdout, "L2 cache run missing L2-miss stats")

    syntax_result = run([str(nexusc), "parse", str(bad_syntax)])
    require(syntax_result.returncode != 0, "parse should fail on invalid syntax")
    require("expected ';' after variable declaration" in syntax_result.stderr, "syntax diagnostic missing")

    semantic_result = run([str(nexusc), "check", str(bad_semantics)])
    require(semantic_result.returncode != 0, "check should fail on invalid semantics")
    require("undefined identifier" in semantic_result.stderr, "semantic diagnostic missing")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
