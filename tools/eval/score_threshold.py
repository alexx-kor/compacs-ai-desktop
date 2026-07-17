# -*- coding: utf-8 -*-
"""Top-1 dense cosine similarity / distance for threshold tuning (dev-only)."""
from __future__ import annotations

import json
import math
import struct
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VECTORS = ROOT / "vectors.bin"
if not VECTORS.exists():
    VECTORS = ROOT / "build" / "Release" / "vectors.bin"

QUESTIONS = [
    ("in", "monitor", "Что показывает экран Монитор?"),
    ("in", "sync", "Как синхронизировать данные прибора?"),
    ("in", "address", "Что такое панель адреса текущего субъекта?"),
    ("in", "terminal", "Как запустить графический терминал КОМПАКС?"),
    ("in", "orders", "Какие предписания отображаются на экране Монитор?"),
    ("out", "unicorn", "What is the color of a unicorn in COMPACS docs XYZ123?"),
    ("out", "pizza", "Как приготовить пиццу Маргарита?"),
    ("out", "weather", "Какая погода завтра в Москве?"),
    ("out", "bitcoin", "Сколько стоит биткоин?"),
    ("out", "cats", "Как научить кота говорить?"),
]


def read_vectors(path: Path):
    data = path.read_bytes()
    if data[:8] != b"COMPACS1":
        raise SystemExit(f"bad magic in {path}")
    # header: magic8 + version u32 + count u32 + dim u32 + reserved u32
    _ver, n, dim, _res = struct.unpack_from("<IIII", data, 8)
    off = 24
    rows = []
    for _ in range(n):
        # id u32, page u32, source_len u16, text_len u32, source, text, dim*f32
        _id, page = struct.unpack_from("<II", data, off)
        off += 8
        slen = struct.unpack_from("<H", data, off)[0]
        off += 2
        tlen = struct.unpack_from("<I", data, off)[0]
        off += 4
        source = data[off : off + slen].decode("utf-8", "replace")
        off += slen
        text = data[off : off + tlen].decode("utf-8", "replace")
        off += tlen
        vec = list(struct.unpack_from(f"<{dim}f", data, off))
        off += dim * 4
        rows.append((page, source, text, vec))
    return dim, rows


def embed(text: str) -> list[float]:
    body = json.dumps({"content": text}).encode("utf-8")
    req = urllib.request.Request(
        "http://127.0.0.1:8081/embedding",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=90) as resp:
        payload = json.loads(resp.read().decode("utf-8"))
    if isinstance(payload, list) and payload and isinstance(payload[0], dict):
        value = payload[0].get("embedding")
    elif isinstance(payload, list):
        value = payload
    else:
        value = payload.get("embedding")
    while isinstance(value, list) and value and isinstance(value[0], list):
        value = value[0]
    return [float(x) for x in value]


def cosine(a: list[float], b: list[float]) -> float:
    dot = sum(x * y for x, y in zip(a, b))
    na = math.sqrt(sum(x * x for x in a))
    nb = math.sqrt(sum(y * y for y in b))
    if na == 0 or nb == 0:
        return 0.0
    return dot / (na * nb)


def main() -> None:
    dim, rows = read_vectors(VECTORS)
    print(f"vectors={VECTORS} n={len(rows)} dim={dim}", flush=True)
    results = []
    for kind, label, q in QUESTIONS:
        qv = embed(q)
        best_sim = -1.0
        best = None
        for page, source, _text, vec in rows:
            sim = cosine(qv, vec)
            if sim > best_sim:
                best_sim = sim
                best = (page, source)
        dist = 1.0 - best_sim
        page, source = best
        safe = source.encode("ascii", "replace").decode("ascii")
        print(f"{kind:3} | {label:10} | sim={best_sim:.6f} | dist={dist:.6f} | {safe} p.{page}")
        results.append((kind, label, best_sim, dist))
    worst_in = min((r for r in results if r[0] == "in"), key=lambda x: x[2])
    best_out = max((r for r in results if r[0] == "out"), key=lambda x: x[2])
    print(f"gap: worst_in_sim={worst_in[2]:.6f} best_out_sim={best_out[2]:.6f}")
    print(f"suggested distance threshold mid={(worst_in[3] + best_out[3]) / 2:.6f}")


if __name__ == "__main__":
    main()
