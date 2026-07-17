# -*- coding: utf-8 -*-
"""Control latency bench: Monitor / sync / OOD (dev-only)."""
from __future__ import annotations

import os
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

CONTROL = [
    ("monitor", "Что показывает экран Монитор?"),
    ("sync", "Как синхронизировать данные прибора?"),
    ("unicorn", "What is the color of a unicorn in COMPACS docs XYZ123?"),
]


def find_main() -> Path:
    env = os.environ.get("COMPACS_MAIN")
    if env:
        return Path(env)
    for cand in (ROOT / "build" / "Release" / "main.exe", ROOT / "main.exe"):
        if cand.exists():
            return cand
    raise SystemExit("main.exe not found")


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
    return {
        "app_ms": int(app_m[-1]) if app_m else None,
        "wall_ms": wall_ms,
        "not_found": "NOT FOUND" in out.upper(),
        "err": err.strip()[:120],
        "exit": p.returncode,
    }


def main() -> None:
    exe = find_main()
    print(f"exe={exe}", flush=True)
    for label, q in CONTROL:
        r = ask(exe, q)
        print(
            f"{label:8} APP_MS={r['app_ms']} WALL_MS={r['wall_ms']} "
            f"NF={r['not_found']} ERR={r['err']!r}",
            flush=True,
        )


if __name__ == "__main__":
    main()
