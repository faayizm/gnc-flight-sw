#!/usr/bin/env python3
"""
Check that every relative link in the repository's Markdown resolves.

This repository has more documentation than code: a README in every directory,
eighteen lessons, and generated reference material. Cross-links between them
are how a reader navigates, and a broken one is a small betrayal -- especially
in a lesson aimed at someone who does not yet know their way around.

So it is checked, in CI, like anything else that matters.

Run:  make check-links
"""

from __future__ import annotations

import pathlib
import re
import sys

# Markdown link, but not an image, and not preceded by a backtick.
LINK = re.compile(r'(?<!!)\[([^\]\n]*)\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
FENCE = re.compile(r'^\s*(```|~~~)')

SKIP_DIRS = {".git", "build", ".venv", "__pycache__", "node_modules"}


def strip_code(text: str) -> str:
    """
    Blank out fenced code blocks and inline code spans.

    Without this, a C++ lambda like [](void*) inside an example reads as a
    Markdown link to a file called "void*" -- which is exactly the false
    positive that made the first version of this script useless.
    """
    out, in_fence = [], False
    for line in text.split("\n"):
        if FENCE.match(line):
            in_fence = not in_fence
            out.append("")
            continue
        out.append("" if in_fence else re.sub(r'`[^`\n]*`', "``", line))
    return "\n".join(out)


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()

    broken: list[tuple[pathlib.Path, int, str, str]] = []
    checked = 0
    files = 0

    for md in sorted(root.rglob("*.md")):
        if any(part in SKIP_DIRS for part in md.parts):
            continue
        files += 1
        text = strip_code(md.read_text())
        for line_no, line in enumerate(text.split("\n"), start=1):
            for label, target in LINK.findall(line):
                if target.startswith(("http://", "https://", "mailto:", "#")):
                    continue
                path_part = target.split("#")[0]
                if not path_part:
                    continue
                checked += 1
                if not (md.parent / path_part).resolve().exists():
                    broken.append((md.relative_to(root), line_no, label, target))

    print(f"checked {checked} relative links across {files} Markdown files")

    if broken:
        print(f"\n{len(broken)} broken:\n")
        for path, line_no, label, target in broken:
            print(f"  {path}:{line_no}")
            print(f"      [{label}]({target})")
        return 1

    print("all resolve")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
