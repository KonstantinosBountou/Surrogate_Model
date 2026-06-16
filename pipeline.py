import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.stats import qmc


DEFAULT_NS3_PATH = Path.home() / "ns-allinone-3.47" / "ns-3.47"
DEFAULT_OUT_DIR = Path.home() / "parametric_outputs"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run NS-3 parametric simulations, train the surrogate model, and validate it."
    )
    parser.add_argument("--ns3-path", default=str(DEFAULT_NS3_PATH))
    parser.add_argument("--program", default="parametric")
    parser.add_argument("--out-dir", default=str(DEFAULT_OUT_DIR))
    parser.add_argument("--samples", type=int, default=40)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--sim-time", type=float, default=10.0)

    parser.add_argument("--distance-min", type=float, default=50.0)
    parser.add_argument("--distance-max", type=float, default=300.0)
    parser.add_argument("--txpower-min", type=float, default=30.0)
    parser.add_argument("--txpower-max", type=float, default=43.0)
    parser.add_argument("--bandwidth-min", type=float, default=10.0)
    parser.add_argument("--bandwidth-max", type=float, default=80.0)
    parser.add_argument("--numues-min", type=int, default=2)
    parser.add_argument("--numues-max", type=int, default=10)
    parser.add_argument("--numerology-min", type=int, default=0)
    parser.add_argument("--numerology-max", type=int, default=2)

    parser.add_argument("--scenario", default="UMa")
    parser.add_argument("--channel-condition", default="LOS")
    parser.add_argument("--shadowing-enabled", action="store_true")
    parser.add_argument("--scheduler", default="TdmaPF")
    parser.add_argument("--packet-size", type=int, default=1200)
    parser.add_argument("--app-data-rate", default="1500Kbps")

    parser.add_argument("--skip-surrogate", action="store_true")
    parser.add_argument("--skip-validation", action="store_true")
    return parser.parse_args()


def ns3_command(ns3_path, program, params):
    run_arg = " ".join([program] + [f"--{key}={value}" for key, value in params.items()])
    return [str(ns3_path / "ns3"), "run", run_arg]


def generate_samples(args):
    sampler = qmc.LatinHypercube(d=5, seed=args.seed)
    raw = sampler.random(n=args.samples)

    scaled = qmc.scale(
        raw,
        [
            args.distance_min,
            args.txpower_min,
            args.bandwidth_min,
            args.numues_min,
            args.numerology_min,
        ],
        [
            args.distance_max,
            args.txpower_max,
            args.bandwidth_max,
            args.numues_max,
            args.numerology_max,
        ],
    )

    samples = []
    for row in scaled:
        samples.append(
            {
                "distance": int(round(row[0])),
                "txPowerGnb": round(float(row[1]), 1),
                "bwMhz": int(round(row[2])),
                "numUes": int(np.clip(round(row[3]), args.numues_min, args.numues_max)),
                "numerology": int(np.clip(round(row[4]), args.numerology_min, args.numerology_max)),
                "simTime": args.sim_time,
                "scenario": args.scenario,
                "channelCondition": args.channel_condition,
                "shadowingEnabled": str(bool(args.shadowing_enabled)).lower(),
                "scheduler": args.scheduler,
                "packetSize": args.packet_size,
                "appDataRate": args.app_data_rate,
            }
        )

    return samples


def run_simulations(args, out_csv, samples):
    if out_csv.exists():
        out_csv.unlink()

    failed = []

    for i, params in enumerate(samples, start=1):
        params = dict(params)
        params["outFile"] = out_csv

        print(
            f"\n[{i:02d}/{len(samples)}] "
            f"dist={params['distance']}m | tx={params['txPowerGnb']}dBm | "
            f"bw={params['bwMhz']}MHz | ues={params['numUes']} | num={params['numerology']}"
        )

        result = subprocess.run(
            ns3_command(Path(args.ns3_path), args.program, params),
            cwd=args.ns3_path,
            capture_output=True,
            text=True,
            timeout=900,
        )

        if result.returncode != 0:
            print("  FAILED")
            print(result.stderr[-1200:])
            failed.append(i)
        else:
            print("  OK")

    return failed


def main():
    args = parse_args()

    ns3_path = Path(args.ns3_path).expanduser().resolve()
    out_dir = Path(args.out_dir).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    out_csv = out_dir / "results.csv"
    config_path = out_dir / "run_config.json"

    samples = generate_samples(args)

    print("=" * 60)
    print("Step 1: Running NS-3 parametric simulations")
    print("=" * 60)
    print(f"ns-3 path: {ns3_path}")
    print(f"Output CSV: {out_csv}")
    print(f"Samples: {len(samples)}")

    failed = run_simulations(args, out_csv, samples)

    if not out_csv.exists():
        raise RuntimeError("No results CSV was created. All simulations may have failed.")

    df = pd.read_csv(out_csv)
    df.to_csv(out_csv, index=False)

    print("\n" + "=" * 60)
    print("Dataset summary")
    print("=" * 60)
    print(f"Successful rows: {len(df)}")
    print(f"Failed simulation indices: {failed if failed else 'none'}")
    print(df.describe(include="all").round(4))

    config = {
        "ns3_path": str(ns3_path),
        "program": args.program,
        "out_dir": str(out_dir),
        "results_csv": str(out_csv),
        "model_path": str(out_dir / "gp_models.pkl"),
        "plot_path": str(out_dir / "surrogate_results.png"),
        "validation_csv": str(out_dir / "validation.csv"),
        "samples": len(samples),
        "sim_time": args.sim_time,
        "defaults": {
            "scenario": args.scenario,
            "channelCondition": args.channel_condition,
            "shadowingEnabled": bool(args.shadowing_enabled),
            "scheduler": args.scheduler,
            "packetSize": args.packet_size,
            "appDataRate": args.app_data_rate,
        },
    }

    config_path.write_text(json.dumps(config, indent=2), encoding="utf-8")
    script_dir = Path(__file__).resolve().parent

    if not args.skip_surrogate:
        print("\n" + "=" * 60)
        print("Step 2: Training surrogate model")
        print("=" * 60)
        subprocess.run(
            [
                sys.executable,
                str(script_dir / "surrogate_model_fixed.py"),
                "--csv",
                str(out_csv),
                "--model",
                config["model_path"],
                "--plot",
                config["plot_path"],
            ],
            check=True,
        )

    if not args.skip_validation:
        print("\n" + "=" * 60)
        print("Step 3: Running validation")
        print("=" * 60)
        subprocess.run(
            [
                sys.executable,
                str(script_dir / "validation_fixed.py"),
                "--config",
                str(config_path),
            ],
            check=True,
        )

    print("\nDone.")
    print(f"Outputs folder: {out_dir}")


if __name__ == "__main__":
    main()
