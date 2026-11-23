# Dataset 1440 train and test

Reproducible package for the 1440-run CBR study using ns-3.47 and 5G-LENA. It contains the simulation inputs and outputs, the fixed training/validation split, training code, saved selected models and evaluation results. The delay models are those of **1440**, not the experimental 1440_v2 models.

## 1. Contents

For a file-by-file explanation of provenance and purpose, see the [Greek documentation guide](documentation/README.md).

| Folder | Contents |
|---|---|
| `dataset/` | Original 1,440-row `dataset.csv`, feature schema, and row-level metadata with relative simulation paths |
| `configurations/` | 1,440 run folders, each containing `config.json` and `results.json` |
| `codes/` | Training and comparison scripts, dependency versions and weight policy |
| `test-configurations/` | 24 test inputs, 120 measured runs and their original JSON inputs/outputs |
| `results/reference/delay/` | Original delay training/validation reports, benchmark results and two selected HARQ-specific models |
| `results/reference/remaining_metrics/` | Original reports for throughput, jitter and packet loss, and their three selected models |
| `results/reference/comparison/` | Combined four-metric comparison produced by `compare.py --results reference` |
| `results/reproduced/` | Locally regenerated models/results; excluded from Git |

The raw JSON files preserve the original simulation configuration. Training and comparison do not require ns-3, SSH or n8n. This package supplies completed simulations; rerunning the simulator requires the original corrected scenario and ns-3 build, which are not installed by the commands below. Machine-specific historical paths in archived reports are provenance only and are not needed by the portable scripts.

### Main CSV files and where to find them

All paths below are relative to **`Dataset-1440-train-test/`** in this repository. Click a path to open the file.

| Purpose | CSV path | What it contains |
|---|---|---|
| Original dataset | [dataset/dataset.csv](dataset/dataset.csv) | 1,440 simulation rows with configuration inputs and all four measured outputs; five runs per configuration. |
| Authoritative raw-run split | [dataset/metadata.csv](dataset/metadata.csv) | One record per dataset row, in matching order. `partition` assigns each run to training or validation; also includes simulation ID, seed/run, relative JSON paths and `validation_scope`. |
| Configuration-level split | [results/reference/remaining_metrics/split_manifest.csv](results/reference/remaining_metrics/split_manifest.csv) | Assignment of 288 configuration means to 226 training and 62 validation cases. All five repetitions stay together. |
| Training rows | [results/reference/remaining_metrics/train_runs.csv](results/reference/remaining_metrics/train_runs.csv) | The 1,130 raw simulation rows assigned to training. |
| Validation rows | [results/reference/remaining_metrics/validation_runs.csv](results/reference/remaining_metrics/validation_runs.csv) | The 310 raw simulation rows assigned to validation. |
| Aggregated training inputs and targets | [results/reference/delay/train_means.csv](results/reference/delay/train_means.csv) | 226 configuration means, including all four target metrics and their estimated mean variances. These are the targets used for fitting. |
| Aggregated validation inputs and targets | [results/reference/delay/validation_means.csv](results/reference/delay/validation_means.csv) | 62 configuration means used for validation. |
| Validation results for throughput, jitter and packet loss | [results/reference/remaining_metrics/validation_metrics.csv](results/reference/remaining_metrics/validation_metrics.csv) | MAE, RMSE and R² for every candidate and evaluation subset. Use `subset = all` for the complete 62-case validation set. |
| Selected models for the three targets | [results/reference/remaining_metrics/selected_models.csv](results/reference/remaining_metrics/selected_models.csv) | Validation scores and the selected algorithm for each of throughput, jitter and packet loss. |
| Validation results for delay | [results/reference/delay/validation_metrics.csv](results/reference/delay/validation_metrics.csv) | MAE, RMSE and R² for the HARQ-routed delay predictor, overall and by validation subset. |
| Internal selection results for delay | [results/reference/delay/inner_metrics.csv](results/reference/delay/inner_metrics.csv) | Candidate performance on three grouped inner holdouts drawn only from training, separately for HARQ-on and HARQ-off. |
| Internal split for delay selection | [results/reference/delay/inner_split_manifest.csv](results/reference/delay/inner_split_manifest.csv) | Training/holdout membership for each internal split and HARQ regime. |
| Test configuration inputs | [test-configurations/configurations_24.csv](test-configurations/configurations_24.csv) | The 24 configuration vectors supplied to the trained models. |
| Actual test simulation measurements | [test-configurations/ns3_runs_120.csv](test-configurations/ns3_runs_120.csv) | Five actual ns-3 runs per test configuration, 120 rows total. `compare.py` aggregates these to obtain the actual target means. |
| Test scores for all four metrics | [results/reference/comparison/benchmark24_metrics_all.csv](results/reference/comparison/benchmark24_metrics_all.csv) | Overall benchmark MAE, RMSE, R² and negative-prediction counts. This is the main test-results summary. |
| Actual versus predicted test values | [results/reference/comparison/benchmark24_comparison_all_metrics.csv](results/reference/comparison/benchmark24_comparison_all_metrics.csv) | The 24 configurations with actual means, predictions and absolute errors for all four metrics, side by side. |

**Training results versus training-set scores:** the validation and inner-holdout reports above describe evaluation during model development. We have **not** saved a separate MAE/RMSE/R² table evaluated on the same observations used to fit the models. `train_runs.csv` and `train_means.csv` contain training data, not training-error scores. The archived `remaining_metrics/train_means.csv` and `validation_means.csv` contain only its three targets; the corresponding `delay/` files listed above include all four.

After running the supplied training commands, new reports appear under `results/reproduced/remaining_metrics/` and `results/reproduced/delay/`. Running `python codes/compare.py --results reproduced` writes the two combined test-results CSVs under `results/reproduced/comparison/`. The published `results/reference/` files remain unchanged.

## 2. Install dependencies

Tested with **Python 3.12.13 on Ubuntu/WSL**. Use Python 3.12 and install the pinned packages from `codes/requirements.txt`: NumPy, pandas, SciPy, scikit-learn, CatBoost and joblib. Pickle/joblib models must only be loaded from trusted sources and should use the recorded dependency versions.

```bash
git clone https://github.com/KonstantinosBountou/Surrogate_Model.git
cd Surrogate_Model/Dataset-1440-train-test
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -r codes/requirements.txt
```

Windows PowerShell activation instead:

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r codes/requirements.txt
```

## 3. Dataset and split

One UE, one gNB, CBR uplink, Friis propagation with fading/shadowing disabled, and distances between 100 and 200 m. Each configuration has five repetitions using seed 42 and runs 1–5. Traffic is generated from 1 to 11 s, followed by a 2 s drain interval.

The 1,440 raw rows represent **288 configuration means**, including 99 distinct families when distance is excluded. Each metric target is the arithmetic mean of five runs. Sample variance divided by five supplies the estimated variance of the mean for the GP noise term.

| Partition | Raw runs | Configuration means |
|---|---:|---:|
| Training | 1,130 | 226 |
| Validation | 310 | 62 |
| Separate diagnostic benchmark | 120 | 24 |

The benchmark is additional to the 1,440 rows. Dataset evolution was 960 + 120 + 120 + 240 runs. The split is fixed in `dataset/metadata.csv` (`partition`, `validation_scope`), not randomized on each training run. All five repetitions of a configuration stay together. The original split was preserved during enrichment. Targeted families can occur at different distances in both partitions: this evaluates within-family interpolation, not exclusively unseen-family generalization.

No target values, run identifiers or file paths are model inputs. Numeric preprocessing and categorical encoding are fitted on training data only. Saved models are not refitted on validation observations.

## 4. Train

Run in this order from the package directory:

```bash
python codes/train_remaining.py
python codes/train_delay.py
```

The first command aggregates all four measured targets and trains **throughput, jitter and packet loss**. Candidates are ExtraTrees, CatBoost, GaussianProcess, weighted ExtraTrees and weighted CatBoost. The selected model minimizes validation RMSE for each target. The weighted variants use the supplied heuristic policy; selected GaussianProcess models do not use those sample weights. Final models are fitted on training only.

The second command uses the same aggregation and split to train **delay separately for HARQ-on and HARQ-off**. Per regime it evaluates ExtraTrees and CatBoost with raw and log1p targets. Selection uses mean RMSE across three grouped inner holdouts drawn solely from training. These are repeated holdouts, not three exhaustive disjoint cross-validation folds. The 1440 choices are ExtraTrees/raw for HARQ-on and CatBoost/raw for HARQ-off.

Regeneration preserves `results/reference/`. The scripts stop when completed regenerated results already exist. Archive/rename `results/reproduced/` before intentionally starting another experiment.

## 5. Predict and compare with ns-3

After training:

```bash
python codes/compare.py --results reproduced
```

To use the supplied selected models without retraining:

```bash
python codes/compare.py --results reference
```

The script reads the 24 input configurations, predicts all four targets, recomputes actual means from `test-configurations/ns3_runs_120.csv`, joins by configuration ID and writes:

- `results/<reference|reproduced>/comparison/benchmark24_comparison_all_metrics.csv`: inputs, actual means, predictions and absolute errors.
- `results/<reference|reproduced>/comparison/benchmark24_metrics_all.csv`: MAE, RMSE, R² and negative-prediction counts.

The test repetitions use seed 42 and runs 1001–1005. These are existing measured ns-3 outputs; comparison does not rerun the simulator. Historical repeated use of this benchmark guided dataset enrichment, so it is **diagnostic, not an untouched final test**. New independent configurations are required to assess generalization without this adaptive reuse.

## 6. Results and interpretation

| Metric | Benchmark MAE | Benchmark RMSE | R² |
|---|---:|---:|---:|
| Throughput | 2.172271 Mbps | 2.847553 Mbps | 0.996948 |
| Delay | 13.695249 ms | 24.073212 ms | 0.750764 |
| Jitter | 0.024036 ms | 0.041077 ms | 0.881527 |
| Packet loss | 1.906380 pp | 2.646625 pp | 0.932925 |

Throughput has the strongest fit on this benchmark. Delay remains difficult near sharp transitions despite improvement over the 960-run baseline (MAE 26.71 ms). Packet loss has **8 negative benchmark predictions** and 14 negative predictions on validation for the selected GP. These are physically invalid; all reported scores use raw predictions without clipping. A high R² does not remove the need to address those bounds.

MAE is mean absolute error; RMSE gives larger errors more influence. R² = 1 − SSE/SST compares squared errors with predicting the evaluation-set mean: 1 is perfect, 0 matches that baseline, negative values are worse. R² is not percentage accuracy. Packet loss targets are percentages, so absolute errors are percentage points (pp).

Validation reports reside separately under each target group's `validation_metrics.csv`; they must not be confused with benchmark metrics. Small one-case subsets have undefined R². The main validation sets contain 62 means. The top-level delay reports contain only delay; `remaining_metrics` reports contain only the other three targets.
