"""Correlate PresentMon raw QPC with VRS phases; do not mix swapchains or instrumented frames."""
import argparse
import bisect
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


def number(value):
    """Missing telemetry remains absent, including nonfinite CSV values."""
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (TypeError, ValueError):
        return None


def percentile(values, fraction):
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    low, high = math.floor(index), math.ceil(index)
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def analyze(session):
    """Use one PC's QPC frequency, explicit stable phases, and marked profiler exclusions."""
    manifest = json.loads((session / "manifest.json").read_text(encoding="utf-8-sig"))
    events = [json.loads(line) for line in (session / "process.jsonl").read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    samples = [row for row in events if row.get("type") == "sample"]
    if not samples:
        raise ValueError("No collector samples")
    phases, config = [], {}
    for row in samples:
        config.update(row.get("config_vrs", {}))
        report = row.get("vrs", {}).get("report", {})
        if row.get("vrs", {}).get("status") != "ok" or "requested" not in report:
            continue
        key = (report.get("requested"), report.get("applied"), report.get("stereo"),
               *(config.get(k) for k in ("xr_vrs_width", "xr_vrs_height", "xr_vrs_offset_x", "xr_vrs_offset_y")))
        if not phases or phases[-1]["key"] != key:
            phases.append({"key": key, "start_qpc_ms": row["qpc_ms"]})
    if not phases:
        raise ValueError("No valid VRS phases")
    exclusions, start = [], None
    for event in events:
        if event.get("type") == "gpu_profile_start":
            start = event["qpc_ms"]
        elif event.get("type") in ("gpu_profile_end", "gpu_profile_error") and start is not None:
            exclusions.append((start, event["exclude_until_qpc_ms"]))
            start = None
    if start is not None:
        exclusions.append((start, math.inf))
    starts = [phase["start_qpc_ms"] for phase in phases]
    sample_times = [sample["qpc_ms"] for sample in samples]
    buckets = defaultdict(lambda: {"frame_ms": [], "gpu_busy_ms": []})
    skipped = defaultdict(int)
    with (session / "frames.csv").open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        fields = reader.fieldnames or []
        if "CPUStartQPC" not in fields:
            raise ValueError("Expected raw CPUStartQPC (--qpc_time); refusing to guess timestamp units")
        frame_column = next((name for name in ("FrameTime", "MsBetweenAppStart") if name in fields), None)
        if not frame_column:
            raise ValueError("Missing supported PresentMon frame-duration column")
        for row in reader:
            if row.get("ProcessID") and int(row["ProcessID"]) != manifest["process_id"]:
                skipped["other_process"] += 1
                continue
            tick, duration = number(row["CPUStartQPC"]), number(row[frame_column])
            if tick is None or duration is None or duration <= 0:
                skipped["missing_duration_or_time"] += 1
                continue
            time = tick * 1000 / manifest["qpc_frequency"]
            sample_index = bisect.bisect_right(sample_times, time) - 1
            if sample_index < 0 or time - sample_times[sample_index] > 1500 or samples[sample_index].get("vrs", {}).get("status") != "ok":
                skipped["unobserved_or_invalid_vrs"] += 1
                continue
            phase = bisect.bisect_right(starts, time) - 1
            if phase < 0 or time < max(samples[0]["qpc_ms"] + 60000, starts[phase] + 20000):
                skipped["warmup_or_transition"] += 1
                continue
            # Include preceding frame work too: FrameTime spans the following CPU-start interval.
            if any(time <= end and time + duration >= begin for begin, end in exclusions):
                skipped["gpu_probe_interval"] += 1
                continue
            if row.get("FrameType") not in (None, "", "Application", "0"):
                skipped["non_application_frame"] += 1
                continue
            bucket = buckets[(phase, row.get("SwapChainAddress", "unknown"))]
            bucket["frame_ms"].append(duration)
            gpu = number(row.get("GPUBusy", row.get("MsGPUBusy")))
            if gpu is not None:
                bucket["gpu_busy_ms"].append(gpu)
    result = []
    for (index, chain), data in sorted(buckets.items()):
        durations, gpu = data["frame_ms"], data["gpu_busy_ms"]
        result.append({"phase": index, "settings": phases[index]["key"], "swapchain": chain,
                       "frames": len(durations), "mean_frame_ms": statistics.mean(durations),
                       "game_cpu_cadence_fps": 1000 / statistics.mean(durations),
                       "p95_frame_ms": percentile(durations, .95), "p99_frame_ms": percentile(durations, .99),
                       "gpu_busy_median_ms": statistics.median(gpu) if gpu else None})
    probes = []
    for path in sorted(session.glob("gpu-profile-*.json")):
        envelope = json.loads(path.read_text(encoding="utf-8-sig"))
        report = envelope["report"]
        frames = report.get("frames", 0)
        groups = report.get("groups", [])
        probes.append({"file": path.name, "valid": envelope["valid"], "context": envelope["context"],
                       "frames": frames,
                       "summed_range_ms_per_frame": sum(g["sum_ms"] for g in groups) / frames if frames else None,
                       "ps_invocations_per_frame": sum(g["ps"][7] for g in groups if len(g.get("ps", [])) == 11) / frames if frames else None})
    return {"semantics": "Game CPU frame cadence by swapchain; not headset scanout. GPU range sums may overlap and are not total frame time.",
            "excluded": dict(skipped), "phases": result, "gpu_probes": probes}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.session), indent=2))
