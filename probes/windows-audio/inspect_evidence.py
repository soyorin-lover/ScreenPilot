"""Validate saved raw evidence consistency, separately from probe acceptance."""
import argparse
import json
from pathlib import Path
from analyze_wav import analyze


def inspect(directory):
    d = json.loads((directory / "result.json").read_text(encoding="utf-8"))
    wav = analyze(directory / "system-mix.wav")
    assert wav["sample_frames"] == d["pcm_frames"]
    assert (wav["rate"], wav["channels"]) == (d["sample_rate"], d["channels"])
    cursor = 0
    previous = None
    position_gaps = []
    for p in d["packets"]:
        assert p["stream_frame_offset"] == cursor
        assert p["pcm_byte_offset"] == cursor * d["channels"] * d["bits_per_sample"] // 8
        assert p["frames"] > 0
        if previous:
            assert p["qpc_100ns"] > previous["qpc_100ns"]
            gap = p["device_position_frames"] - previous["device_position_frames"] - previous["frames"]
            if gap:
                position_gaps.append(gap)
        previous = p
        cursor += p["frames"]
    assert cursor == d["pcm_frames"]
    assert all(a["qpc_100ns"] < b["qpc_100ns"]
               for a, b in zip(d["video_frames"], d["video_frames"][1:]))
    for tone in d["tone_detections"]:
        p = next(p for p in d["packets"] if p["stream_frame_offset"] <= tone["stream_frame_offset"]
                 < p["stream_frame_offset"] + p["frames"])
        assert tone["sample_offset"] == tone["stream_frame_offset"] - p["stream_frame_offset"]
        expected = p["qpc_100ns"] + tone["sample_offset"] * 1e7 / d["sample_rate"]
        assert abs(expected - tone["qpc_100ns"]) <= 1
    for a in d["associations"]:
        if a["valid"]:
            first = next(f for f in d["video_frames"] if f["detected_state"] == a["state"])
            assert first["qpc_100ns"] == a["first_pixel_frame_qpc_100ns"]
            assert abs((first["qpc_100ns"] - a["audio_onset_qpc_100ns"]) / 1e4
                       - a["video_minus_audio_ms"]) < 1e-6
    def ages(items):
        values = sorted((p["poll_qpc_100ns"] - p["qpc_100ns"]) / 1e4 for p in items)
        return {"count": len(values), "min": values[0], "median": values[len(values)//2],
                "max": values[-1], "future_over_1ms": sum(v < -1 for v in values)}
    return {"evidence_consistent": True, "probe_acceptance": d["success"],
            "no_tone": d["no_tone"], "audio_read_minus_stamp_ms": ages(d["packets"]),
            "video_read_minus_stamp_ms": ages(d["video_frames"]),
            "device_position_gaps_frames": position_gaps, "wav_analysis": wav}


if __name__ == "__main__":
    cli = argparse.ArgumentParser()
    cli.add_argument("directory", type=Path)
    args = cli.parse_args()
    print(json.dumps(inspect(args.directory), indent=2))
