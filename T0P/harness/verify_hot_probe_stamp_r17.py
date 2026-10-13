"""Check that S25 probe stamps belong to the measured dispatch, not its post-margin."""
from __future__ import annotations

import argparse
from pathlib import Path


def verify(source_path: Path) -> str:
    source = source_path.read_text(encoding="utf-8")
    start = source.index("void acquire_hot_probe_endpoint(")
    end = source.index("double episode_microseconds(", start)
    body = source[start:end]
    measured = body.index("dispatch({CommandKind::Probe, 'P', kHotProbeK, probe_r});")
    stamp_copy = body.index("target.tsc_start = source.hot_probe_tsc_start", measured)
    post_margin = body.index("run_hot_probe_margin(probe_r);", measured)
    pad_end = body.index("record.qpc_pad_end = qpc_now();", post_margin)
    validation = body.index("S25_HOT_PROBE_WORKER_EVIDENCE_INVALID", pad_end)
    if not measured < stamp_copy < post_margin < pad_end < validation:
        raise ValueError("S25 worker stamps or validations are outside the required order")
    if "source.hot_probe_" in body[post_margin:]:
        raise ValueError("post-margin timestamps can overwrite the measured worker stamps")

    # The old ordering compared a 4.78 s measured region with the 0.596 s
    # post-margin dispatch, producing a false coordinator-overhead HOLD.
    region_s = 4.780637
    measured_span_s = 4.780000
    margin_span_s = 0.596000
    measured_overhead = max(0.0, region_s - measured_span_s) / measured_span_s
    stale_overhead = max(0.0, region_s - margin_span_s) / margin_span_s
    if measured_overhead > 0.01 or stale_overhead <= 0.01:
        raise ValueError("synthetic measured-vs-margin stamp fixture did not distinguish the bug")
    return ("HOT_PROBE_STAMP_ORDER_R17 PASS measured_dispatch<binary_stamp_copy<post_margin<pad_end<validation "
            f"synthetic_measured_overhead={measured_overhead:.6f} stale_margin_overhead={stale_overhead:.6f}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).with_name("residency_r17.cpp"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result + "\n", encoding="utf-8")
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
