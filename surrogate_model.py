import argparse
import pickle
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.gaussian_process import GaussianProcessRegressor
from sklearn.gaussian_process.kernels import ConstantKernel, Matern, WhiteKernel
from sklearn.model_selection import KFold, cross_val_score
from sklearn.preprocessing import StandardScaler


CANONICAL_RENAMES = {
    "bw_mhz": "bandwidth_mhz",
    "txpower_dbm": "txpower_gnb_dbm",
    "txPowerGnb": "txpower_gnb_dbm",
    "numUes": "num_ues",
}

FEATURES = [
    "distance_m",
    "txpower_gnb_dbm",
    "bandwidth_mhz",
    "num_ues",
    "numerology",
]

POSSIBLE_TARGETS = [
    "throughput_mbps",
    "delay_ms",
    "plr_pct",
    "jitter_ms",
    "sinr_db",
    "spectral_eff_bpshz",
    "jain_fairness",
    "harq_rtx_pct",
]


def parse_args():
    parser = argparse.ArgumentParser(description="Train GP surrogate models from NS-3 CSV output.")
    parser.add_argument("--csv", default=str(Path.home() / "parametric_outputs" / "results.csv"))
    parser.add_argument("--model", default=str(Path.home() / "parametric_outputs" / "gp_models.pkl"))
    parser.add_argument("--plot", default=str(Path.home() / "parametric_outputs" / "surrogate_results.png"))
    return parser.parse_args()


def load_dataset(csv_path):
    df = pd.read_csv(csv_path)
    df = df.rename(columns={old: new for old, new in CANONICAL_RENAMES.items() if old in df.columns})

    missing_features = [col for col in FEATURES if col not in df.columns]
    if missing_features:
        raise ValueError(f"Missing feature columns from CSV: {missing_features}")

    targets = [col for col in POSSIBLE_TARGETS if col in df.columns]
    if not targets:
        raise ValueError("No supported target columns found in CSV.")

    keep = FEATURES + targets
    df = df[keep].copy()

    for col in keep:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    df = df.dropna()

    if len(df) < 5:
        raise ValueError(f"Too few valid rows after cleaning: {len(df)}")

    return df, targets


def train_models(df, targets):
    x = df[FEATURES].values

    scaler = StandardScaler()
    x_scaled = scaler.fit_transform(x)

    kernel = (
        ConstantKernel(1.0, (1e-3, 1e3))
        * Matern(length_scale=np.ones(len(FEATURES)), nu=2.5)
        + WhiteKernel(noise_level=1e-3, noise_level_bounds=(1e-8, 1e1))
    )

    models = {}
    constant_targets = {}

    cv_splits = min(5, len(df))
    cv = KFold(n_splits=cv_splits, shuffle=True, random_state=42)

    for target in targets:
        y = df[target].values

        if np.nanstd(y) == 0:
            constant_targets[target] = float(y[0])
            models[target] = None
            print(f"{target}: constant value = {y[0]:.6g}, GP skipped")
            continue

        gp = GaussianProcessRegressor(
            kernel=kernel,
            n_restarts_optimizer=10,
            normalize_y=True,
            random_state=42,
        )

        gp.fit(x_scaled, y)

        try:
            scores = cross_val_score(gp, x_scaled, y, cv=cv, scoring="r2")
            score = np.nanmean(scores)
        except Exception:
            score = np.nan

        models[target] = gp
        print(f"{target}: GP trained, CV R2 = {score:.3f}")
        print(f"  kernel: {gp.kernel_}")

    return models, constant_targets, scaler


def make_plot(df, targets, models, constant_targets, scaler, plot_path):
    distance_min = float(df["distance_m"].min())
    distance_max = float(df["distance_m"].max())
    dist_range = np.linspace(distance_min, distance_max, 100)

    x_pred = np.column_stack(
        [
            dist_range,
            np.full_like(dist_range, float(df["txpower_gnb_dbm"].median())),
            np.full_like(dist_range, float(df["bandwidth_mhz"].median())),
            np.full_like(dist_range, int(round(df["num_ues"].median()))),
            np.full_like(dist_range, int(round(df["numerology"].median()))),
        ]
    )

    x_pred_scaled = scaler.transform(x_pred)

    n_targets = len(targets)
    n_cols = min(3, n_targets)
    n_rows = int(np.ceil(n_targets / n_cols))

    fig, axes = plt.subplots(n_rows, n_cols, figsize=(5.6 * n_cols, 4.2 * n_rows))
    axes = np.array(axes).reshape(-1)

    for ax, target in zip(axes, targets):
        if models[target] is None:
            y_pred = np.full_like(dist_range, constant_targets[target], dtype=float)
            y_std = np.zeros_like(dist_range)
            suffix = "constant"
        else:
            y_pred, y_std = models[target].predict(x_pred_scaled, return_std=True)
            suffix = "GP"

        ax.plot(dist_range, y_pred, linewidth=2, label="Prediction")

        if np.any(y_std > 0):
            ax.fill_between(dist_range, y_pred - 2 * y_std, y_pred + 2 * y_std, alpha=0.2)

        ax.scatter(df["distance_m"], df[target], color="black", s=25, alpha=0.75, label="NS-3")
        ax.set_xlabel("Distance (m)")
        ax.set_ylabel(target)
        ax.set_title(f"{target} - {suffix}")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)

    for ax in axes[n_targets:]:
        ax.axis("off")

    fig.suptitle("5G-LENA surrogate model predictions", fontsize=14)
    fig.tight_layout()

    plot_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(plot_path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    args = parse_args()

    csv_path = Path(args.csv).expanduser().resolve()
    model_path = Path(args.model).expanduser().resolve()
    plot_path = Path(args.plot).expanduser().resolve()

    print("=" * 60)
    print("Loading dataset")
    print("=" * 60)
    print(f"CSV path: {csv_path}")

    df, targets = load_dataset(csv_path)

    print(f"Dataset samples: {len(df)}")
    print(f"Features: {FEATURES}")
    print(f"Targets: {targets}")
    print(df.describe().round(4))

    print("\n" + "=" * 60)
    print("Training Gaussian Process surrogate models")
    print("=" * 60)

    models, constant_targets, scaler = train_models(df, targets)
    make_plot(df, targets, models, constant_targets, scaler, plot_path)

    model_path.parent.mkdir(parents=True, exist_ok=True)

    with open(model_path, "wb") as handle:
        pickle.dump(
            {
                "models": models,
                "constant_targets": constant_targets,
                "scaler": scaler,
                "features": FEATURES,
                "targets": targets,
                "csv_path": str(csv_path),
            },
            handle,
        )

    print("\n" + "=" * 60)
    print("Saved outputs")
    print("=" * 60)
    print(f"Plot saved to: {plot_path}")
    print(f"Models saved to: {model_path}")


if __name__ == "__main__":
    main()
