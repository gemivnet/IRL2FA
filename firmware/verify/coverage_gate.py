#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fails if firmware/core coverage is under the minimum.

Usage: coverage_gate.py summary.json MIN_LINES MIN_BRANCHES
(summary.json from `llvm-cov export -summary-only`). Also writes a Markdown
table to $GITHUB_STEP_SUMMARY when run in GitHub Actions.
"""
import json
import os
import sys


def main():
    path, min_lines, min_branches = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
    data = json.load(open(path))["data"][0]
    rows = []
    for f in sorted(data["files"], key=lambda f: f["filename"]):
        s = f["summary"]
        rows.append((os.path.relpath(f["filename"]), s["lines"]["percent"], s["branches"]["percent"],
                     s["functions"]["percent"]))
    t = data["totals"]
    lines, branches, funcs = t["lines"]["percent"], t["branches"]["percent"], t["functions"]["percent"]

    md = ["| file | lines % | branches % | functions % |", "|-|-:|-:|-:|"]
    md += [f"| {n} | {lp:.1f} | {bp:.1f} | {fp:.1f} |" for n, lp, bp, fp in rows]
    md.append(f"| **total** | **{lines:.1f}** | **{branches:.1f}** | **{funcs:.1f}** |")
    md.append(f"\nMinimum: {min_lines:.0f} % of lines, {min_branches:.0f} % of branches.")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as fh:
            fh.write("### firmware/core coverage (host tests)\n\n" + "\n".join(md) + "\n")

    print(f"firmware/core coverage: {lines:.1f} % lines, {branches:.1f} % branches, "
          f"{funcs:.1f} % functions (minimum {min_lines:.0f} / {min_branches:.0f})")
    ok = lines >= min_lines and branches >= min_branches
    if not ok:
        print("coverage below the minimum", file=sys.stderr)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
