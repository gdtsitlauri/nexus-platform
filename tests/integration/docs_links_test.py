#!/usr/bin/env python3
"""Every relative link and image in the Markdown files must point to a file or folder that exists, and every
#anchor to a heading of the target page (GitHub's heading ids)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)\)")


def strip_code(text: str) -> str:
    return re.sub(r"```.*?```", "", text, flags=re.S)


def heading_ids(path: Path) -> set[str]:
    ids = set()
    for line in strip_code(path.read_text(encoding="utf-8")).splitlines():
        match = re.match(r"#{1,6}\s+(.*)", line)
        if match:
            title = re.sub(r"[`*_]", "", match.group(1).strip().lower())
            ids.add(re.sub(r"[^\w\- ]", "", title).replace(" ", "-"))
    return ids


def main() -> int:
    repo = Path(sys.argv[1]).resolve()
    broken = []
    files = [p for p in repo.rglob("*.md") if p.relative_to(repo).parts[0] not in ("build", ".git")]
    for md in files:
        for target in LINK.findall(strip_code(md.read_text(encoding="utf-8"))):
            if re.match(r"[a-z]+:", target):
                continue
            path, _, anchor = target.partition("#")
            destination = (md.parent / path) if path else md
            if not destination.exists():
                broken.append(f"{md.relative_to(repo)}: {target}")
            elif anchor and destination.suffix == ".md" and anchor not in heading_ids(destination):
                broken.append(f"{md.relative_to(repo)}: {target} (no such heading)")
    if broken:
        raise SystemExit("broken links:\n" + "\n".join(broken))
    print(f"docs links: {len(files)} Markdown files, all relative links and anchors resolve")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
