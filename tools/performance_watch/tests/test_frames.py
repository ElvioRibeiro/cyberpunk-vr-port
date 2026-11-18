"""Synthetic CSV/log integration: units, phase boundaries, exclusions and swapchain isolation."""
import csv
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("frames", Path(__file__).parents[1] / "analyze-frames.py")
frames = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frames)


class FrameAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        (self.root / "manifest.json").write_text(json.dumps({"process_id": 42, "qpc_frequency": 1000}))
        self.events = []
        for t in range(0, 301000, 1000):
            mode = 1 if 100000 <= t < 200000 else 0
            self.events.append({"type": "sample", "qpc_ms": t,
                                "config_vrs": {}, "vrs": {"status": "stale" if t == 170000 else "ok",
                                "report": {"requested": mode, "applied": mode, "stereo": 1}}})
        self.events += [{"type": "gpu_profile_start", "qpc_ms": 140000},
                        {"type": "gpu_profile_end", "qpc_ms": 141000, "exclude_until_qpc_ms": 143000}]
        self.rows = [(70000, "A", 10), (71000, "A", 10), (70000, "B", 20),
                     (130000, "A", 8), (131000, "A", 8), (230000, "A", 10),
                     (105000, "A", 100), (140500, "A", 100), (170100, "A", 100),
                     (999999, "A", 100)]

    def write(self, raw_header="CPUStartQPC"):
        ordered = sorted(self.events, key=lambda event: event["qpc_ms"])
        (self.root / "process.jsonl").write_text("\n".join(json.dumps(event) for event in ordered))
        with (self.root / "frames.csv").open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["ProcessID", raw_header, "SwapChainAddress", "FrameTime", "GPUBusy"])
            writer.writerows((42, t, chain, duration, duration / 2) for t, chain, duration in self.rows)

    def test_independent_phases_and_swapchains(self):
        self.write()
        result = frames.analyze(self.root)
        self.assertEqual([row["frames"] for row in result["phases"]], [2, 1, 2, 1])
        self.assertEqual([row["game_cpu_cadence_fps"] for row in result["phases"]], [100, 50, 125, 100])
        self.assertEqual(result["excluded"]["gpu_probe_interval"], 1)
        self.assertEqual(result["excluded"]["warmup_or_transition"], 1)
        self.assertEqual(result["excluded"]["unobserved_or_invalid_vrs"], 2)

    def test_reject_ambiguous_timestamp_units(self):
        self.write("CPUStartTime")
        with self.assertRaisesRegex(ValueError, "refusing to guess"):
            frames.analyze(self.root)

    def test_unfinished_probe_excludes_remaining_frames(self):
        self.events = [event for event in self.events if event["type"] != "gpu_profile_end"]
        self.write()
        result = frames.analyze(self.root)
        self.assertFalse(any(row["phase"] == 2 for row in result["phases"]))


if __name__ == "__main__":
    unittest.main()
