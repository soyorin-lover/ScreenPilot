"""Inspect actual loopback WAV for the three known probe tones; no dependencies."""
import argparse
import array
import json
import math
import struct
import sys
from pathlib import Path


def load_wave(path):
    raw = Path(path).read_bytes()
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE", "Not a RIFF/WAVE file"
    chunks = {}
    offset = 12
    while offset + 8 <= len(raw):
        tag = raw[offset:offset + 4]
        size = struct.unpack_from("<I", raw, offset + 4)[0]
        assert offset + 8 + size <= len(raw), "Truncated WAV chunk"
        chunks[tag] = raw[offset + 8:offset + 8 + size]
        offset += 8 + size + (size % 2)
    fmt, data = chunks[b"fmt "], chunks[b"data"]
    code, channels, rate, _, align, bits = struct.unpack_from("<HHIIHH", fmt)
    if code == 0xFFFE:
        code = struct.unpack_from("<I", fmt, 24)[0]
    assert (code, bits) in ((3, 32), (1, 16), (1, 32)), "Unsupported WAV sample format"
    assert channels > 0 and rate > 0 and align == channels * bits // 8
    assert len(data) % align == 0
    samples = array.array("f" if code == 3 else "h" if bits == 16 else "i")
    samples.frombytes(data)
    if sys.byteorder != "little":
        samples.byteswap()
    scale = 1 if code == 3 else 2 ** (bits - 1)
    mono = [sum(samples[i:i + channels]) / channels / scale
            for i in range(0, len(samples), channels)]
    assert all(math.isfinite(x) for x in mono), "Non-finite audio sample"
    return rate, channels, mono


def analyze(path):
    rate, channels, samples = load_wave(path)
    size = max(1, rate // 100)  # 10ms windows; onset resolution is at most one window.
    frequencies = (800, 1200, 1600)
    kernels = {hz: [(math.cos(2 * math.pi * hz * i / rate),
                     math.sin(2 * math.pi * hz * i / rate)) for i in range(size)]
               for hz in frequencies}
    hits = {hz: [] for hz in frequencies}
    for start in range(0, len(samples) - size + 1, size):
        block = samples[start:start + size]
        rms = math.sqrt(sum(x * x for x in block) / size)
        if rms < 0.003:
            continue
        for hz, kernel in kernels.items():
            re = sum(x * c for x, (c, _) in zip(block, kernel))
            im = sum(x * s for x, (_, s) in zip(block, kernel))
            amplitude = 2 * math.hypot(re, im) / size
            if amplitude >= 0.004 and amplitude * amplitude / (2 * rms * rms) >= 0.45:
                hits[hz].append((start, rms, amplitude))
    return {"rate": rate, "channels": channels, "sample_frames": len(samples),
            "duration_seconds": len(samples) / rate, "window_frames": size,
            "tones": [{"hz": hz, "matching_windows": len(hits[hz]),
                       "first_sample_frame": hits[hz][0][0] if hits[hz] else None,
                       "last_sample_frame": hits[hz][-1][0] if hits[hz] else None,
                       "peak_amplitude": max((x[2] for x in hits[hz]), default=0)}
                      for hz in frequencies]}


if __name__ == "__main__":
    cli = argparse.ArgumentParser()
    cli.add_argument("wav", type=Path)
    cli.add_argument("--expect", choices=("tones", "no-tones"), required=True)
    args = cli.parse_args()
    result = analyze(args.wav)
    counts = [tone["matching_windows"] for tone in result["tones"]]
    assert all(n >= 5 for n in counts) if args.expect == "tones" else not any(counts), result
    print(json.dumps(result, indent=2))
