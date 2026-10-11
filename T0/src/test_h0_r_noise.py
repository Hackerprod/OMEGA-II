"""Synthetic-only QA for MD/31 nearest-rank R_noise; never touches official data or a timer."""
from __future__ import annotations

import csv
import math
import tempfile
from pathlib import Path

from h0_runner import aggregate_r_noise_cells, calculate_r_noise, nearest_rank_p95_index


def make_pair(directory:Path,n:int)->tuple[Path,Path,float]:
    first=directory/f"A1_{n}.csv";second=directory/f"A2_{n}.csv"
    fields=["rep","episode","worker_key","application_index","kernel_only_ns","status","variant"]
    with first.open("w",newline="",encoding="utf-8") as a,second.open("w",newline="",encoding="utf-8") as b:
        w1=csv.DictWriter(a,fieldnames=fields);w2=csv.DictWriter(b,fieldnames=fields);w1.writeheader();w2.writeheader()
        for i in range(n):
            d=i/1000.0
            t1=1_000_000.0
            t2=t1*(2.0+d)/(2.0-d)
            key={"rep":1,"episode":i,"worker_key":"CORE_API_00_G0_MASK_0_1","application_index":i,
                 "status":"OK","variant":"A"}
            w1.writerow({**key,"kernel_only_ns":repr(t1)})
            w2.writerow({**key,"kernel_only_ns":repr(t2)})
    expected=((n-1 if n==20 else n-2)/1000.0)
    return first,second,expected


assert nearest_rank_p95_index(31)==29
assert nearest_rank_p95_index(20)==18
with tempfile.TemporaryDirectory(prefix="h0_r_noise_synthetic_") as directory:
    root=Path(directory)
    for n,expected_index in ((31,29),(20,18)):
        first,second,_=make_pair(root,n)
        result=calculate_r_noise(first,second)
        assert result["p95_index_zero_based"]==expected_index
        expected=expected_index/1000.0
        assert math.isclose(result["p95_cell"],expected,rel_tol=1e-12,abs_tol=1e-12)
    aggregate=aggregate_r_noise_cells({"cell_AA1":{"status":"CELL_PASS","p95_cell":0.012},
                                      "cell_AA2":{"status":"CELL_PASS","p95_cell":0.019}})
    assert aggregate["R_noise"]==0.019
    assert aggregate["status"]=="PASS"
print('{"record":"h0_r_noise_synthetic_qa","n31_index":29,"n20_index":18,"max_cell_p95_aggregation":true,"measurement":false,"status":"PASS"}')

# Static wiring checks complement the C++ pure statistics/schedule QA.
source=(Path(__file__).parent/"h0_measure.cpp").read_text(encoding="utf-8")
assert "config.calibration_episodes_override=aa_source->calibration.episodes;" in source
assert "pair[0].episodes!=episodes||pair[1].episodes!=episodes" in source
assert "forEachPairedAAStep(episodes," in source
assert source.count("runEpisodeBatch(team,spec,1,collect_qpc,capture_applications)")==2
print('{"record":"h0_aa_retry_wiring_synthetic_qa","original_calibration_replayed":true,"cached_episode_guard":true,"measurement":false,"status":"PASS"}')
