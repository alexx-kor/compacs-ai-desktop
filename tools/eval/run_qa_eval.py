# -*- coding: utf-8 -*-
"""Golden Set runner: ask questions from qa_evaluation.json via main.exe -q (dev-only)."""
from __future__ import annotations

import json
import os
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = Path(__file__).resolve().parent / "qa_evaluation.json"


def find_main() -> Path:
    env = os.environ.get("COMPACS_MAIN")
    if env:
        return Path(env)
    for cand in (ROOT / "build" / "Release" / "main.exe", ROOT / "main.exe"):
        if cand.exists():
            return cand
    raise SystemExit("main.exe not found (build first or set COMPACS_MAIN)")


def ask(exe: Path, question: str) -> dict:
    t0 = time.perf_counter()
    p = subprocess.run(
        [str(exe), "-q", question],
        cwd=str(exe.parent),
        capture_output=True,
        timeout=300,
    )
    wall_ms = int((time.perf_counter() - t0) * 1000)
    out = p.stdout.decode("utf-8", "replace")
    err = p.stderr.decode("utf-8", "replace")
    app_m = re.findall(r"\((\d+)\s*ms\)", out)
    app_ms = int(app_m[-1]) if app_m else None
    not_found = "NOT FOUND" in out.upper()
    has_sources = "sources" in out.lower() or "источник" in out.lower() or re.search(r"p\.\d+", out, re.I)
    return {
        "wall_ms": wall_ms,
        "app_ms": app_ms,
        "not_found": not_found,
        "has_sources": bool(has_sources),
        "exit": p.returncode,
        "err": err.strip()[:200],
        "out_len": len(out),
    }


def main() -> None:
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    items = data.get("items") or []
    exe = find_main()
    print(f"exe={exe} items={len(items)}", flush=True)
    for i, item in enumerate(items, 1):
        q = item.get("question") or ""
        r = ask(exe, q)
        label = q.encode("ascii", "replace").decode("ascii")[:48]
        print(
            f"{i:02d} APP_MS={r['app_ms']} WALL_MS={r['wall_ms']} "
            f"NF={r['not_found']} SRC={r['has_sources']} | {label}",
            flush=True,
        )


if __name__ == "__main__":
    main()
