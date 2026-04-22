#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, check=False)


def require_ok(result: subprocess.CompletedProcess[str], description: str) -> None:
    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or f"{description} failed"
        raise SystemExit(f"{description} failed: {message}")


def count_named_calls(text: str, name: str) -> int:
    pattern = re.compile(rf"\bcall[a-z]*\b[^\n]*\b{name}\b|\bjal\b[^\n]*\b{name}\b")
    return len(pattern.findall(text))


def main() -> int:
    if len(sys.argv) != 4:
      raise SystemExit("usage: run_toolchain_compare.py <nexusc> <repo_root> <output_dir>")

    nexusc = Path(sys.argv[1])
    repo = Path(sys.argv[2])
    output_dir = Path(sys.argv[3])
    output_dir.mkdir(parents=True, exist_ok=True)

    clang = shutil.which("clang")
    if clang is None:
        raise SystemExit("clang was not found in PATH for the bounded toolchain comparison")

    nexus_source = repo / "examples" / "source_lang" / "interproc_fold.nx"
    c_source = repo / "tools" / "toolchain_compare" / "interproc_fold.c"
    nexus_asm = output_dir / "nexus_interproc_fold.s"
    nexus_folded_ir = output_dir / "nexus_interproc_fold.folded.ir"
    clang_o0 = output_dir / "clang_interproc_fold_O0.s"
    clang_o1 = output_dir / "clang_interproc_fold_O1.s"

    compile_result = run([str(nexusc), "compile", str(nexus_source), "-S", "-o", str(nexus_asm)])
    require_ok(compile_result, "nexusc compile")

    fold_result = run([str(nexusc), "opt", str(nexus_source), "--pass", "interproc-constfold"])
    require_ok(fold_result, "nexusc interproc-constfold")
    nexus_folded_ir.write_text(fold_result.stdout)

    common_clang_flags = [
        clang,
        "-S",
        "-fno-asynchronous-unwind-tables",
        "-fno-stack-protector",
        str(c_source),
    ]
    clang_o0_result = run(common_clang_flags + ["-O0", "-o", str(clang_o0)])
    require_ok(clang_o0_result, "clang -S -O0")
    clang_o1_result = run(common_clang_flags + ["-O1", "-o", str(clang_o1)])
    require_ok(clang_o1_result, "clang -S -O1")

    nexus_asm_text = nexus_asm.read_text()
    nexus_folded_text = nexus_folded_ir.read_text()
    clang_o0_text = clang_o0.read_text()
    clang_o1_text = clang_o1.read_text()

    summary = {
        "nexus_asm_call_inc": count_named_calls(nexus_asm_text, "inc"),
        "nexus_folded_ir_call_inc": nexus_folded_text.count("call inc("),
        "clang_O0_call_inc": count_named_calls(clang_o0_text, "inc"),
        "clang_O1_call_inc": count_named_calls(clang_o1_text, "inc"),
        "nexus_asm_lines": len(nexus_asm_text.splitlines()),
        "clang_O0_lines": len(clang_o0_text.splitlines()),
        "clang_O1_lines": len(clang_o1_text.splitlines()),
    }

    summary_json = output_dir / "toolchain_comparison_summary.json"
    summary_json.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

    summary_md = output_dir / "toolchain_comparison_summary.md"
    summary_md.write_text(
        "# Toolchain Comparison Summary\n\n"
        "Compared a tiny pure-call example across the Nexus Phase 9 interprocedural prototype and"
        " local Clang assembly generation.\n\n"
        "| Artifact | Command | Calls to `inc` | Lines |\n"
        "| --- | --- | ---: | ---: |\n"
        f"| Nexus assembly | `nexusc compile examples/source_lang/interproc_fold.nx -S` | {summary['nexus_asm_call_inc']} | {summary['nexus_asm_lines']} |\n"
        f"| Nexus folded IR | `nexusc opt examples/source_lang/interproc_fold.nx --pass interproc-constfold` | {summary['nexus_folded_ir_call_inc']} | {len(nexus_folded_text.splitlines())} |\n"
        f"| Clang -O0 | `clang -S -O0 tools/toolchain_compare/interproc_fold.c` | {summary['clang_O0_call_inc']} | {summary['clang_O0_lines']} |\n"
        f"| Clang -O1 | `clang -S -O1 tools/toolchain_compare/interproc_fold.c` | {summary['clang_O1_call_inc']} | {summary['clang_O1_lines']} |\n\n"
        "Interpretation:\n"
        "- Nexus assembly keeps the explicit call structure because the Phase 4 backend lowers the original IR.\n"
        "- The Phase 9 `interproc-constfold` pass removes bounded constant calls in the IR before backend lowering.\n"
        "- Clang `-O0` normally preserves the call, while `-O1` may inline or otherwise eliminate it on the tiny analogue.\n",
    )

    print(f"Generated toolchain comparison report in {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
