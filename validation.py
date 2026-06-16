import argparse
import json
import pickle
import subprocess
from pathlib import Path

import numpy as np
import pandas as pd


FEATURE_TO_PARAM = {
    "distance_m": "distance",
    "txpower_gnb_dbm": "txPowerGnb",
    "bandwidth_mhz": "bwMhz",
    "num_ues": "numUes",
    "numerology": "numerology",
}


def parse_args():
    parser = argparse.ArgumentParser(description="Validate trained surrogate models with new NS-3 runs.")
    parser.add_argument("--config", default=str(Path.home() / "parametric_outputs" / "run_config.json"))
    parser.add_argument("--points", type=int, default=3)
    return parser.parse_args()


def ns3_command(ns3_path, program, params):
    run_arg = " ".join([program] + [f"--{key}={value}" for key, value in params.items()])
    return [str(ns3_path / "ns3"), "run", run_arg]


def load_model(model_path):
    if not model_path.exists():
        raise FileNotFoundError(f"Cannot find {model_path}. Run surrogate_model_fixed.py first.")

    with open(model_path, "rb") as handle:
        data = pickle.load(handle)

    return (
        data["models"],
        data["scaler"],
        data.get("constant_targets", {}),
        data["features"],
        data["targets"],
        Path(data["csv_path"]),
    )


def build_validation_points(training_csv, features, count, defaults):
    df_train = pd.read_csv(training_csv)

    if "bw_mhz" in df_train.columns and "bandwidth_mhz" not in df_train.columns:
        df_train = df_train.rename(columns={"bw_mhz": "bandwidth_mhz"})

    quantiles = np.linspace(0.2, 0.8, count)
    points = []

    for q in quantiles:
        row = {}

        for feature in features:
            value = float(df_train[feature].quantile(q))

            if feature in {"distance_m", "bandwidth_mhz", "num_ues", "numerology"}:
                value = int(round(value))
            elif feature == "txpower_gnb_dbm":
                value = round(value, 1)

            row[FEATURE_TO_PARAM[feature]] = value

        row.update(defaults)
        points.append(row)

    return points


def run_validation_simulations(config, features, training_csv, points_count):
    ns3_path = Path(config["ns3_path"])
    program = config.get("program", "parametric")
    validation_csv = Path(config["validation_csv"]).expanduser().resolve()
    defaults = config.get("defaults", {})

    val_points = build_validation_points(training_csv, features, points_count, defaults)

    if validation_csv.exists():
        validation_csv.unlink()

    print("Running validation simulations...")

    for i, params in enumerate(val_points, start=1):
        params = dict(params)
        params["outFile"] = validation_csv

        print(
            f"\n[{i}/{len(val_points)}] "
            f"dist={params.get('distance')}m | tx={params.get('txPowerGnb')}dBm | "
            f"bw={params.get('bwMhz')}MHz | ues={params.get('numUes')} | "
            f"num={params.get('numerology')}"
        )

        result = subprocess.run(
            ns3_command(ns3_path, program, params),
            cwd=ns3_path,
            capture_output=True,
            text=True,
            timeout=900,
        )

        if result.returncode != 0:
            print(result.stderr[-1200:])
            raise RuntimeError(f"Validation simulation failed for point {i}")

    return validation_csv, val_points


def main():
    args = parse_args()

    config_path = Path(args.config).expanduser().resolve()
    config = json.loads(config_path.read_text(encoding="utf-8"))

    model_path = Path(config["model_path"]).expanduser().resolve()
    models, scaler, constant_targets, features, targets, training_csv = load_model(model_path)

    validation_csv, val_points = run_validation_simulations(
        config,
        features,
        training_csv,
        args.points,
    )

    df_val = pd.read_csv(validation_csv)

    if "bw_mhz" in df_val.columns and "bandwidth_mhz" not in df_val.columns:
        df_val = df_val.rename(columns={"bw_mhz": "bandwidth_mhz"})

    missing = [col for col in features if col not in df_val.columns]
    if missing:
        raise ValueError(f"Validation CSV is missing feature columns: {missing}")

    x_val = df_val[features].values
    x_val_scaled = scaler.transform(x_val)

    print("\n" + "=" * 60)
    print("VALIDATION RESULTS")
    print("=" * 60)

    for metric in targets:
        if metric not in df_val.columns:
            print(f"\n{metric}: skipped because it is not in validation CSV")
            continue

        y_true = df_val[metric].values
        model = models.get(metric)

        print(f"\n{metric}:")

        if model is None:
            const_value = constant_targets.get(metric, np.nan)

            for j in range(len(df_val)):
                print(
                    f"  point {j + 1}: NS3={y_true[j]:.3f}, "
                    f"constant_training_value={const_value:.3f}"
                )

            continue

        y_pred, y_std = model.predict(x_val_scaled, return_std=True)
        within_ci = np.abs(y_true - y_pred) <= 2 * y_std

        for j in range(len(df_val)):
            status = "within 95% CI" if within_ci[j] else "outside 95% CI"

            print(
                f"  point {j + 1}: "
                f"NS3={y_true[j]:.3f}, GP={y_pred[j]:.3f}+/-{2 * y_std[j]:.3f}, {status}"
            )

    print(f"\nValidation CSV: {validation_csv}")


if __name__ == "__main__":
    main()
