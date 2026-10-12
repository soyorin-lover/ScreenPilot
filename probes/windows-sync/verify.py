"""Independently check saved W02 evidence (Python standard library only)."""
import argparse
import json
import statistics
from pathlib import Path


def check(result, expected):
    frames = result["frames"]
    events = result["events"]
    origin = result["origin_qpc_100ns"]
    start = result["pause_start_qpc_100ns"]
    end = result["pause_end_qpc_100ns"]
    assert len(frames) >= 10, "No real capture evidence"
    assert result["error"] == "", result["error"]
    assert result["pause_boundary_checks_passed"]
    assert result["wrong_dimensions"] == 0
    assert result["coordinate_failures"] == 0
    assert any(f["paused"] for f in frames)
    assert any(f["raw_qpc_100ns"] >= end for f in frames)
    times = [f["raw_qpc_100ns"] for f in frames]
    assert all(a < b for a, b in zip(times, times[1:]))
    for sample in frames + events:
        raw = sample["raw_qpc_100ns"]
        # Independently integrate the overlap with the pause interval.
        paused_duration = max(0, min(raw, end) - start)
        assert sample["active_100ns"] == raw - origin - paused_duration
        assert sample["paused"] == (start <= raw < end)
    ages = []
    for frame in frames:
        assert frame["readback_end_qpc_100ns"] >= frame["readback_start_qpc_100ns"]
        age = (frame["readback_start_qpc_100ns"] - frame["raw_qpc_100ns"]) / 10000
        assert age >= -1, "Frame timestamp appears in the future"
        ages.append(age)
    if expected == "positive":
        assert result["passed"] and not result["input_interference"]
        assert not result["synthetic_release_unconfirmed"]
        matches = result["click_visual_latencies"]
        assert len(matches) == 3
        assert {c["state"] for c in matches} == {1, 2, 3}
        downs = {e["raw_qpc_100ns"] for e in events
                 if e["message"] == 513 and e["probe_injected"]}
        for click in matches:
            assert click["hook_qpc_100ns"] in downs
            candidates = [f for f in frames if f["pixel_state"] == click["state"]
                          and f["raw_qpc_100ns"] >= click["hook_qpc_100ns"]]
            assert candidates and candidates[0]["raw_qpc_100ns"] == click["frame_qpc_100ns"]
            assert abs(click["latency_ms"] - (click["frame_qpc_100ns"] -
                       click["hook_qpc_100ns"]) / 10000) < 0.0001
            for axis in ("x", "y"):
                assert abs(click[f"qt_logical_{axis}"] * result["dpr"] -
                           click[f"hook_client_physical_{axis}"]) <= 2
        positions = result["positions"]
        assert len(positions) == 2
        assert (positions[0]["x"], positions[0]["y"]) != (positions[1]["x"], positions[1]["y"])
    else:
        assert not result["passed"] and not result["input_enabled"]
        assert result["matched_clicks"] == 0 and not events
        assert result["reason"] == "expected_click_evidence_missing"
    return {"expected": expected, "verified": True, "frames": len(frames),
            "events": len(events), "readback_age_ms": {
                "min": min(ages), "median": statistics.median(ages),
                "p95": sorted(ages)[int((len(ages) - 1) * 0.95)], "max": max(ages)}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("result", type=Path)
    parser.add_argument("--expect", required=True, choices=("positive", "negative"))
    args = parser.parse_args()
    result = json.loads(args.result.read_text(encoding="utf-8"))
    print(json.dumps(check(result, args.expect), indent=2))
