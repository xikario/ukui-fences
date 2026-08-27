#!/usr/bin/env python3
"""Summarize Fence lensing JSONL telemetry without skewing by window size."""

import json
import sys
from pathlib import Path


def weighted_mean(rows, value_key, weight_key):
    pairs = [
        (float(row[value_key]), int(row.get(weight_key, 0)))
        for row in rows
        if value_key in row and int(row.get(weight_key, 0)) > 0
    ]
    total = sum(weight for _, weight in pairs)
    return sum(value * weight for value, weight in pairs) / total if total else None


if len(sys.argv) != 2:
    raise SystemExit("usage: analyze_lensing_log.py liquid-glass-demo.jsonl")

events = []
for line in Path(sys.argv[1]).read_text(encoding="utf-8").splitlines():
    try:
        events.append(json.loads(line))
    except (json.JSONDecodeError, ValueError):
        pass

installed = [event for event in events if event.get("event") == "demo_installed"]
ready = [event for event in events if event.get("event") == "shader_ready"]
metrics = [event for event in events if event.get("event") == "render_metrics"]

if installed:
    event = installed[-1]
    print("config:")
    for key in (
        "active_frame_ms", "idle_refresh", "rim_band_px", "edge_band_px",
        "base_lens_strength_px", "velocity_boost_px", "velocity_norm_px_s",
        "response", "center_transmission", "specular_gain",
        "gpu_timer_requested",
    ):
        if key in event:
            print(f"  {key}: {event[key]}")

if ready:
    event = ready[-1]
    print("gpu:")
    for key in (
        "gl_vendor", "gl_renderer", "gl_version",
        "gpu_timer_supported", "gpu_timer_enabled",
    ):
        if key in event:
            print(f"  {key}: {event[key]}")

if not metrics:
    raise SystemExit("no render_metrics entries")

# The first window contains context/texture warm-up and is reported separately.
steady = metrics[1:] if len(metrics) > 1 else metrics
cpu_avg = weighted_mean(steady, "avg_submit_ms", "frames")
gpu_avg = weighted_mean(steady, "avg_gpu_ms", "gpu_samples")
cpu_peak = max(float(row.get("max_submit_ms", 0)) for row in steady)
gpu_peak = max(float(row.get("max_gpu_ms", 0)) for row in steady)

print("metrics:")
print(f"  windows: {len(metrics)}")
print(f"  frames_logged: {sum(int(row.get('frames', 0)) for row in metrics)}")
print(f"  warmup_cpu_submit_ms: {float(metrics[0].get('avg_submit_ms', 0)):.4f}")
print(f"  steady_cpu_submit_avg_ms: {cpu_avg:.4f}" if cpu_avg is not None else
      "  steady_cpu_submit_avg_ms: n/a")
print(f"  steady_cpu_submit_peak_ms: {cpu_peak:.4f}")
print(f"  steady_gpu_avg_ms: {gpu_avg:.4f}" if gpu_avg is not None else
      "  steady_gpu_avg_ms: n/a")
print(f"  steady_gpu_peak_ms: {gpu_peak:.4f}")
print(f"  peak_velocity_px_s: {max(float(row.get('max_velocity_px_s', 0)) for row in metrics):.2f}")
print(f"  peak_lens_strength_px: {max(float(row.get('max_lens_strength_px', 0)) for row in metrics):.2f}")
print(f"  timer_active_windows: {sum(row.get('timer_active') is True for row in metrics)}")
print(f"  timer_sleep_windows: {sum(row.get('timer_active') is False for row in metrics)}")
