"""T0.1/H0 scaffold runner: unique preflights/order registration only; no official timing mode."""
from __future__ import annotations

import hashlib
import csv
import json
import re
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

T0=Path(__file__).resolve().parent.parent
RESULTS=T0/"results"/"T0.1"
LOGS=T0/"logs"
PLAN=RESULTS/"h0_measurement_plan.json"
TOPOLOGY=T0/"results"/"T0.0"/"b_topologia.csv"
EXE=T0/"src"/"build"/"h0_scaffold.exe"
MANIFEST=LOGS/"COMMANDS.json"
PS=Path(r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")
POWER_GUID="8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"
CPU_NAME="13th Gen Intel(R) Core(TM) i7-13700F"
TOPOLOGY_SHA256="8E5CDA8410873852C3DD266E2CC1535376DEEA3705328173BC879117F4574CAF"


def unique_path(path:Path)->None:
    if path.exists():raise FileExistsError(f"refusing to overwrite H0 segment artifact: {path}")


def log_process(name:str,argv:list[str],measurement:bool=False,timeout:int=180)->tuple[int,bytes,bytes]:
    for suffix in ("command.txt","stdout.txt","stderr.txt"):unique_path(LOGS/f"{name}.{suffix}")
    started=datetime.now(timezone.utc).isoformat()
    result=subprocess.run(argv,cwd=LOGS,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False,timeout=timeout)
    (LOGS/f"{name}.command.txt").write_text(subprocess.list2cmdline(argv)+"\n",encoding="utf-8")
    (LOGS/f"{name}.stdout.txt").write_bytes(result.stdout)
    (LOGS/f"{name}.stderr.txt").write_bytes(result.stderr)
    manifest=json.loads(MANIFEST.read_text(encoding="utf-8"))
    manifest.append({"name":name,"started_utc":started,
        "completed_utc":datetime.now(timezone.utc).isoformat(),"command":subprocess.list2cmdline(argv),
        "exit_code":result.returncode,"measurement":measurement,"authorization_token_required":measurement,
        "stdout_file":f"{name}.stdout.txt","stdout_sha256":hashlib.sha256(result.stdout).hexdigest().upper(),
        "stderr_file":f"{name}.stderr.txt","stderr_sha256":hashlib.sha256(result.stderr).hexdigest().upper()})
    MANIFEST.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    update_hashes([LOGS/f"{name}.command.txt",LOGS/f"{name}.stdout.txt",LOGS/f"{name}.stderr.txt",MANIFEST])
    return result.returncode,result.stdout,result.stderr


def variant_blocks()->list[tuple[str,str]]:
    blocks=[("00_R_NOISE_AA_8P_W294912_K16_m1","AA")]
    p_targets=(262152,294912,524286,786438,1048572,1310724,1572858,1835010,2097144,2621448,3145734)
    for w in p_targets:blocks.append((f"04_P8_Knee_K16_m1_W{w}_A", "A"))
    for k in (1,2,4,8,16):blocks.append((f"05_P8_W294912_K{k}_m1","ABC" if k in (8,16) else "AB"))
    # K=16,m=1 A/B/C above supplies the shared H0-R1/matrixization m=1 cell.
    for m in (2,8,16,32):blocks.append((f"07_P8_K16_W294912_m{m}_A","A"))
    blocks.append(("08_P8_R4_K16_W294912_m4","ABC"))
    for k,m in ((1,32),(2,16),(4,8),(8,4),(16,2),(2,32),(4,16),(8,8),(16,4)):
        blocks.append((f"09_P8_FLOPS_K{k}_m{m}_W294912","AB"))
    blocks.append(("10_4P_K16_m1_W589824","ABC"))
    blocks.append(("11_SMT_16LP_K16_m1_W294912_A","A"))
    e_targets=(262152,524286,786438,1048572,1310724)
    for cluster in (0,1):
        for w in e_targets:blocks.append((f"12_Ecluster{cluster}_A_K16_m1_W{w}","A"))
        blocks.append((f"12_Ecluster{cluster}_arch_K16_m1_W589824","ABC"))
    blocks.append(("13_PplusE_K16_m1_W147456","ABC"))
    ids=[key for key,_ in blocks]
    if len(ids)!=len(set(ids)):raise ValueError("duplicate preregistration block id")
    return blocks


def cell_parameters(block_id:str)->dict:
    m=re.fullmatch(r"04_P8_Knee_K16_m1_W(\d+)_A",block_id)
    if m:return {"setup":"P8","W":int(m.group(1)),"K":16,"m":1,"seed_group":"P8_KNEE_W_SWEEP"}
    m=re.fullmatch(r"05_P8_W294912_K(\d+)_m1",block_id)
    if m:return {"setup":"P8","W":294912,"K":int(m.group(1)),"m":1,"seed_group":"P8_W294912_K16_MATRIX"}
    m=re.fullmatch(r"07_P8_K16_W294912_m(\d+)_A",block_id)
    if m:return {"setup":"P8","W":294912,"K":16,"m":int(m.group(1)),"seed_group":"P8_W294912_K16_MATRIX"}
    m=re.fullmatch(r"08_P8_R4_K16_W294912_m4",block_id)
    if m:return {"setup":"P8","W":294912,"K":16,"m":4,"seed_group":"P8_W294912_K16_MATRIX"}
    m=re.fullmatch(r"09_P8_FLOPS_K(\d+)_m(\d+)_W294912",block_id)
    if m:return {"setup":"P8","W":294912,"K":int(m.group(1)),"m":int(m.group(2)),"seed_group":"P8_FIXED_WORK_ARCH"}
    if block_id=="00_R_NOISE_AA_8P_W294912_K16_m1":
        return {"setup":"P8","W":294912,"K":16,"m":1,"seed_group":"P8_W294912_K16_MATRIX"}
    if block_id=="10_4P_K16_m1_W589824":
        return {"setup":"P4","W":589824,"K":16,"m":1,"seed_group":"P4_ARCH_K16_M1"}
    if block_id=="11_SMT_16LP_K16_m1_W294912_A":
        return {"setup":"SMT16","W":294912,"K":16,"m":1,"seed_group":"P8_W294912_K16_MATRIX"}
    m=re.fullmatch(r"12_Ecluster([01])_A_K16_m1_W(\d+)",block_id)
    if m:return {"setup":f"E{m.group(1)}","W":int(m.group(2)),"K":16,"m":1,"seed_group":f"E{m.group(1)}_W_SWEEP"}
    m=re.fullmatch(r"12_Ecluster([01])_arch_K16_m1_W589824",block_id)
    if m:return {"setup":f"E{m.group(1)}","W":589824,"K":16,"m":1,"seed_group":f"E{m.group(1)}_ARCH_K16_M1"}
    if block_id=="13_PplusE_K16_m1_W147456":
        return {"setup":"PplusE","W":147456,"K":16,"m":1,"seed_group":"PPLUS_E_ARCH_K16_M1"}
    raise ValueError(f"no frozen H0 cell configuration for block {block_id}")


def data_seed(group:str)->int:
    base=0x4D44333044415441
    digest=int.from_bytes(hashlib.sha256(group.encode("utf-8")).digest()[:8],"little")
    return (base^digest)&0xFFFFFFFFFFFFFFFF


def nearest_rank_p95_index(n:int)->int:
    if n<=0:raise ValueError("nearest-rank p95 requires n>0")
    return (95*n+99)//100-1


def calculate_r_noise(first:Path,second:Path)->dict:
    def load(path):
        with path.open(newline="",encoding="utf-8-sig") as f:
            return {(r["rep"],r["episode"],r["worker_key"],r["application_index"]):float(r["kernel_only_ns"])
                    for r in csv.DictReader(f) if r["status"]=="OK" and r["variant"]=="A"}
    a,b=load(first),load(second)
    if not a or set(a)!=set(b):
        return {"status":"H0_INVALID","reason":"A/A paired kernel-only observations do not have identical keys/counts","paired_count":len(set(a)&set(b))}
    diffs=[abs(a[k]-b[k])/((a[k]+b[k])*0.5) for k in a if (a[k]+b[k])>0]
    if len(diffs)!=len(a):return {"status":"H0_INVALID","reason":"zero/invalid paired A/A duration","paired_count":len(diffs)}
    diffs.sort();index=nearest_rank_p95_index(len(diffs));p95=diffs[index]
    return {"status":"CELL_PASS" if p95<=0.05 else "CELL_FAIL","p95_cell":p95,"paired_count":len(diffs),
            "p95_index_zero_based":index,"p95_order_statistic":"ceil(0.95*n)-1; empirical nearest-rank, no interpolation","threshold":0.05}


def aggregate_r_noise_cells(cells:dict[str,dict])->dict:
    if not cells or any(cell.get("status") not in ("CELL_PASS","CELL_FAIL") for cell in cells.values()):
        return {"status":"H0_INVALID","reason":"one or more A/A cells lack a complete p95_cell result","cells":cells}
    values={name:float(cell["p95_cell"]) for name,cell in cells.items()}
    r_noise=max(values.values())
    return {"status":"PASS" if r_noise<=0.05 else "H0_INVALID","R_noise":r_noise,
            "p95_cell_by_exact_AA_cell":values,"aggregation":"max(p95_cell) over every A/A cell participating in H0 adjudication",
            "threshold":0.05}


def check_variant_order(path:Path,blocks:list[tuple[str,str]])->dict:
    order=json.loads(path.read_text(encoding="utf-8"))
    if order.get("seed_hex","").upper()!="4D44333048305331" or order.get("measurement") is not False:
        raise RuntimeError("H0 variant order seed/measurement marker differs from the frozen plan")
    if [row.get("block") for row in order.get("blocks",[])]!=[b[0] for b in blocks]:
        raise RuntimeError("H0 preregistered block order does not match the frozen cell schedule")
    for row,(_,codes) in zip(order["blocks"],blocks):
        expected=sorted(codes);actual=sorted("A" if x=="AVX2" else "B" if x=="AVX-VNNI-256" else x for x in row.get("variants",[]))
        if actual!=expected:raise RuntimeError(f"invalid randomized variant block {row.get('block')}")
    return order


def save_segment_status(path:Path,status:dict)->None:
    path.write_text(json.dumps(status,indent=2)+"\n",encoding="utf-8")
    update_hashes([path])


def run_official(segment:str)->dict:
    plan=json.loads(PLAN.read_text(encoding="utf-8"))
    if plan.get("status")!="FROZEN_FOR_OFFICIAL_TIMING":
        raise RuntimeError("H0 plan is still scaffold-only; JUEZ review must freeze it before official timing")
    if plan.get("R_noise_scaffold_pairing",{}).get("status")!="SOL_CONFIRMED":
        raise RuntimeError("R_noise paired-unit/percentile convention needs explicit Sol confirmation before any official H0 timing")
    if not TOPOLOGY.is_file() or not EXE.is_file():raise FileNotFoundError("H0 executable/topology is missing")
    raw_dir=RESULTS/"h0_raw"/segment
    if raw_dir.exists():raise FileExistsError(f"refusing to overwrite H0 segment raw outputs: {raw_dir}")
    raw_dir.mkdir(parents=True)
    status_path=RESULTS/f"h0_execution_status_{segment}.json"
    summary_path=RESULTS/f"h0_session_summary_{segment}.json"
    unique_path(status_path);unique_path(summary_path)
    status={"SESSION":"RUNNING","segment_id":segment,"performance_series_started":False,
            "preflight":None,"variant_order":None,"completed_cells":[],"official_timing_performed":False}
    save_segment_status(status_path,status);save_segment_status(summary_path,status)
    try:
        pre=preflight(segment);status["preflight"]=pre
        if not pre.get("session_preflight_pass"):raise RuntimeError("SESSION=HOLD: H0 preflight gate failed before any series")
        blocks=variant_blocks();order_path=RESULTS/f"h0_variant_order_{segment}.json"
        if not order_path.is_file():
            prepared=prepare_order(segment);order_path=RESULTS/f"h0_variant_order_{segment}.json"
        order=check_variant_order(order_path,blocks);status["variant_order"]={"path":str(order_path.relative_to(T0)),"seed_hex":order["seed_hex"],"block_count":len(order["blocks"])}

        # D1: validate the eviction mechanism and freeze its size before the paired R_noise series.
        eviction_path=raw_dir/"eviction_validation_8MiB.json";unique_path(eviction_path)
        seed=0x4D44333045564943
        code,_,stderr=log_process(f"h0_eviction_validation_8MiB_{segment}",[str(EXE),"--eviction-test","AUTORIZADO",str(TOPOLOGY),str(eviction_path),"8",f"{seed:016X}"],measurement=True,timeout=600)
        if code!=0:raise RuntimeError(f"8 MiB eviction check failed to execute: {stderr.decode('utf-8',errors='replace')}")
        eviction=json.loads(eviction_path.read_text(encoding="utf-8"));eviction_size=8
        if eviction.get("status")!="PASS":
            eviction_path16=raw_dir/"eviction_validation_16MiB.json";unique_path(eviction_path16)
            code,_,stderr=log_process(f"h0_eviction_validation_16MiB_{segment}",[str(EXE),"--eviction-test","AUTORIZADO",str(TOPOLOGY),str(eviction_path16),"16",f"{seed:016X}"],measurement=True,timeout=600)
            if code!=0:raise RuntimeError(f"16 MiB eviction fallback failed to execute: {stderr.decode('utf-8',errors='replace')}")
            eviction=json.loads(eviction_path16.read_text(encoding="utf-8"));eviction_size=16
            if eviction.get("status")!="PASS":raise RuntimeError("H0_INVALID/HOLD: eviction validation failed at 8 and 16 MiB; no third adaptation")
        status["eviction_validation"]=eviction;status["frozen_eviction_mib_per_worker"]=eviction_size
        save_segment_status(status_path,status);save_segment_status(summary_path,status)
        status["performance_series_started"]=True;save_segment_status(status_path,status);save_segment_status(summary_path,status)

        # Preregistered A/A pair is run in one process and shares the exact same base-shard address.
        aa_block="00_R_NOISE_AA_8P_W294912_K16_m1"
        aa_variants=next(r["variants"] for r in order["blocks"] if r["block"]==aa_block)
        if len(aa_variants)!=2 or aa_variants!=["A","A"]:raise RuntimeError("A/A preflight block must be exactly A then A")
        aa_params=cell_parameters(aa_block);aa_seed=data_seed(aa_params["seed_group"])
        aa_block_id=f"{segment}_{aa_block}"
        aa_argv=[str(EXE),"--run-block","AUTORIZADO",str(TOPOLOGY),str(raw_dir),aa_block_id,aa_params["setup"],"AA",
                 str(aa_params["W"]),str(aa_params["K"]),str(aa_params["m"]),f"{aa_seed:016X}",str(eviction_size)]
        aa_code,aa_stdout,aa_stderr=log_process(f"h0_series_{aa_block_id}",aa_argv,measurement=True,timeout=1800)
        if aa_code:raise RuntimeError(f"A/A block failed: {aa_stderr.decode('utf-8',errors='replace')}")
        aa_result=json.loads(aa_stdout.decode("utf-8"))
        if aa_result.get("record")!="h0_block_complete" or aa_result.get("variant_count")!=2:
            raise RuntimeError("A/A block completion marker invalid")
        aa1=aa_block_id+"_A_1";aa2=aa_block_id+"_A_2"
        for cell_id in (aa1,aa2):
            for suffix in (".csv",".status.csv",".summary.csv"):update_hashes([raw_dir/f"{cell_id}{suffix}"])
        update_hashes([raw_dir/"engineering_calibration_only.csv"])
        aa1_raw=raw_dir/f"{aa1}.csv";aa2_raw=raw_dir/f"{aa2}.csv"
        noise_cell=calculate_r_noise(aa1_raw,aa2_raw)
        noise=aggregate_r_noise_cells({aa_block:noise_cell})
        noise_path=raw_dir/"R_noise_preflight.json";unique_path(noise_path);noise_path.write_text(json.dumps(noise,indent=2)+"\n",encoding="utf-8")
        status["R_noise_preflight"]=noise;status["completed_cells"].extend([aa1,aa2]);save_segment_status(status_path,status);save_segment_status(summary_path,status)
        if noise["status"]!="PASS":raise RuntimeError("H0_MEASUREMENT_INVALID: R_noise exceeds preregistered limit or A/A pairing is incomplete")

        for block in order["blocks"]:
            if block["block"]==aa_block:continue
            params=cell_parameters(block["block"]);seed=data_seed(params["seed_group"])
            if any(variant not in ("A","B","C") for variant in block["variants"]):raise RuntimeError("unknown preregistered H0 variant")
            block_id=f"{segment}_{block['block']}"
            argv=[str(EXE),"--run-block","AUTORIZADO",str(TOPOLOGY),str(raw_dir),block_id,params["setup"],"".join(block["variants"]),
                  str(params["W"]),str(params["K"]),str(params["m"]),f"{seed:016X}",str(eviction_size)]
            code,stdout,stderr=log_process(f"h0_series_{block_id}",argv,measurement=True,timeout=1800)
            if code!=0:raise RuntimeError(f"H0 block failed/held at {block_id}: {stderr.decode('utf-8',errors='replace')}")
            result=json.loads(stdout.decode("utf-8"))
            if result.get("record")!="h0_block_complete":raise RuntimeError(f"H0 block completion marker missing: {block_id}")
            for variant_index,variant in enumerate(block["variants"]):
                cell_id=f"{block_id}_{variant}_{variant_index+1}"
                for suffix in (".csv",".status.csv",".summary.csv"):update_hashes([raw_dir/f"{cell_id}{suffix}"])
            update_hashes([raw_dir/"engineering_calibration_only.csv"])
            status["completed_cells"].append(block_id);save_segment_status(status_path,status);save_segment_status(summary_path,status)
        status["SESSION"]="COMPLETE";status["status"]="RAW_SERIES_COMPLETE_AWAITING_OFFLINE_DERIVATION_AND_JUEZ_VERDICT"
        status["official_timing_performed"]=True
        save_segment_status(status_path,status);save_segment_status(summary_path,status)
        return status
    except Exception as exc:
        status["SESSION"]="HOLD";status["status"]="H0_HOLD";status["reason"]=f"{type(exc).__name__}: {exc}"
        save_segment_status(status_path,status);save_segment_status(summary_path,status)
        raise


def prepare_order(segment:str)->dict:
    if not EXE.is_file():raise FileNotFoundError(f"H0 scaffold executable missing: {EXE}")
    block_path=RESULTS/f"h0_variant_blocks_{segment}.txt"
    order_path=RESULTS/f"h0_variant_order_{segment}.json"
    unique_path(block_path);unique_path(order_path)
    blocks=variant_blocks()
    block_path.write_text("".join(f"{name},{variants}\n" for name,variants in blocks),encoding="utf-8")
    argv=[str(EXE),"--prepare-variant-order","4D44333048305331",str(block_path),str(order_path)]
    code,stdout,stderr=log_process(f"h0_prepare_variant_order_{segment}",argv,measurement=False)
    if code!=0:raise RuntimeError(f"H0 variant order preparation failed: {stderr.decode('utf-8',errors='replace')}")
    order=json.loads(order_path.read_text(encoding="utf-8"))
    if len(order.get("blocks",[]))!=len(blocks) or order.get("measurement") is not False:
        raise RuntimeError("prepared H0 variant order does not match the frozen block list")
    update_hashes([block_path,order_path])
    return {"blocks_file":str(block_path.relative_to(T0)),"order_file":str(order_path.relative_to(T0)),"block_count":len(blocks)}


def update_hashes(extra:list[Path])->None:
    sums=T0/"SHA256SUMS.txt";entries={}
    for line in sums.read_text(encoding="utf-8").splitlines():
        parts=line.split(None,1)
        if len(parts)==2:entries[parts[1].strip()]=parts[0]
    for path in extra:entries[path.relative_to(T0).as_posix()]=""
    for rel in list(entries):
        path=T0/rel
        if path.is_file():entries[rel]=hashlib.sha256(path.read_bytes()).hexdigest().upper()
        else:del entries[rel]
    sums.write_text("".join(f"{entries[rel]}  {rel}\n" for rel in sorted(entries)),encoding="utf-8")


def preflight(segment:str)->dict:
    if not TOPOLOGY.is_file():raise FileNotFoundError(f"frozen topology missing: {TOPOLOGY}")
    topology_hash=hashlib.sha256(TOPOLOGY.read_bytes()).hexdigest().upper()
    ps=rf"""
$ErrorActionPreference='Stop'
$pw=(powercfg.exe /getactivescheme | Out-String)
$pm=[regex]::Match($pw,'[0-9a-fA-F]{{8}}-[0-9a-fA-F]{{4}}-[0-9a-fA-F]{{4}}-[0-9a-fA-F]{{4}}-[0-9a-fA-F]{{12}}')
$cpu=@(Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name)
$lp=(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$chrome0=@(Get-Process chrome -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
$samples=@()
for($i=0;$i -lt 11;$i++){{$r=Get-CimInstance -ClassName Win32_PerfFormattedData_PerfOS_Processor -Filter 'Name=''_Total''';$samples += [pscustomobject]@{{utc=(Get-Date).ToUniversalTime().ToString('o');cpu=$r.PercentProcessorTime}};if($i -lt 10){{Start-Sleep -Seconds 1}}}}
$chrome1=@(Get-Process chrome -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
$mean=($samples|Measure-Object cpu -Average).Average;$max=($samples|Measure-Object cpu -Maximum).Maximum
[ordered]@{{power_guid=if($pm.Success){{$pm.Value.ToLowerInvariant()}}else{{''}};cpu_names=$cpu;logical_processors=$lp;chrome_before=$chrome0;chrome_after=$chrome1;cpu_samples=$samples;mean_cpu_percent=$mean;max_cpu_percent=$max}}|ConvertTo-Json -Depth 5 -Compress
"""
    name=f"h0_preflight_{segment}"
    code,stdout,stderr=log_process(name,[str(PS),"-NoProfile","-NonInteractive","-Command",ps],measurement=False,timeout=90)
    if code!=0:raise RuntimeError(f"H0 preflight command failed: {stderr.decode('utf-8',errors='replace')}")
    evidence=json.loads(stdout.decode("utf-8"))
    checks={"chrome_absent":not evidence["chrome_before"] and not evidence["chrome_after"],
            "power_plan_matches":evidence["power_guid"].lower()==POWER_GUID,
            "hardware_matches":evidence["cpu_names"]==[CPU_NAME] and int(evidence["logical_processors"])==24,
            "topology_matches":topology_hash==TOPOLOGY_SHA256,
            "cpu_load_observation_only":True}
    passed=all(checks.values())
    status={"SESSION":"PREFLIGHT_PASS_WAITING_FOR_H0_AUTHORIZATION" if passed else "HOLD",
            "segment_id":segment,"session_preflight_pass":passed,"checks":checks,"preflight":evidence,
            "topology_sha256":topology_hash,"performance_series_started":False,
            "official_timing_performed":False,"official_timing_mode_enabled":False,
            "h0_plan":"results/T0.1/h0_measurement_plan.json"}
    for path in (RESULTS/f"h0_preflight_status_{segment}.json",RESULTS/f"h0_preflight_summary_{segment}.json"):
        unique_path(path);path.write_text(json.dumps(status,indent=2)+"\n",encoding="utf-8")
    update_hashes([RESULTS/f"h0_preflight_status_{segment}.json",RESULTS/f"h0_preflight_summary_{segment}.json",
                   LOGS/f"{name}.command.txt",LOGS/f"{name}.stdout.txt",LOGS/f"{name}.stderr.txt",MANIFEST])
    return status


def main()->int:
    if not PLAN.is_file():raise SystemExit(f"H0 plan missing: {PLAN}")
    plan=json.loads(PLAN.read_text(encoding="utf-8"))
    if "AUTORIZADO" not in plan.get("official_timing_mode","") or plan.get("official_timing_performed") is not False:
        raise SystemExit("H0 runner requires an explicit authorization-gated plan and no prior timing")
    args=sys.argv[1:]
    if len(args)==2 and args[0]=="--prepare-order-only":
        print(json.dumps(prepare_order(args[1]),indent=2));return 0
    if len(args) in (1,2) and args[0]=="--preflight-only":
        segment=args[1] if len(args)==2 else "scaffold_"+datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        if not segment.replace("_","").replace("-","").isalnum():raise SystemExit("segment identifier must be alphanumeric, '_' or '-'")
        status=preflight(segment);print(json.dumps({"SESSION":status["SESSION"],"checks":status["checks"],"files":
            [f"results/T0.1/h0_preflight_status_{segment}.json",f"results/T0.1/h0_preflight_summary_{segment}.json"]},indent=2))
        return 0 if status["session_preflight_pass"] else 1
    if len(args)==3 and args[0]=="--run-official" and args[1]=="AUTORIZADO":
        segment=args[2]
        if not segment.replace("_","").replace("-","").isalnum():raise SystemExit("segment identifier must be alphanumeric, '_' or '-'")
        status=run_official(segment);print(json.dumps({"SESSION":status["SESSION"],"completed_cells":len(status["completed_cells"]),
            "status_file":f"results/T0.1/h0_execution_status_{segment}.json","official_timing_performed":status["official_timing_performed"]},indent=2));return 0
    raise SystemExit("usage: h0_runner.py --preflight-only [unique_segment_id] | --prepare-order-only <unique_segment_id> | --run-official AUTORIZADO <unique_segment_id>")


if __name__=="__main__":main()
