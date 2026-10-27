# Dataset preparation, training and validation workflow

This document describes the workflow used in the local `Surrogate_Model1` project. The repository's `Dataset/` folder contains the preparation stage. The training entry points and `code/` files listed below are currently local; this document does not imply that they have been uploaded. The older model scripts at the repository root are not the entry points for this workflow.

## Main execution flow

```text
run_all.cmd
└── run_training.py
    ├── data/prepare_weights.py
    │   ├── data/resplit_for_coverage.py
    │   └── data/export_metadata.py
    ├── code/compare_dataset1_weights.py
    ├── code/train_and_validate.py
    └── code/predict.py
```

`run_all.cmd` starts the orchestrator. The preparation entry point automatically calls the splitting and metadata-export scripts in the required order.

## Stages and responsibilities

| Stage | Code files (local project paths) | What they do |
|---|---|---|
| 1. Start the workflow | `run_all.cmd` → `run_training.py` | Coordinate preparation, checks, cross-validation, training and prediction checks. Create a separate results directory for each run and collect logs. |
| 2. Prepare data and weights | `data/prepare_weights.py` | Read the base data and metadata CSVs, check alignment and completeness, assign study coefficients as sample weights, and construct groups. |
| 3. Check the split | `data/resplit_for_coverage.py` | Preserve a valid existing split or assign whole groups to training/validation with study-coverage constraints. Called automatically by preparation. Targets and model scores are not used to select the split. |
| 4. Export metadata | `data/export_metadata.py` | Separate auxiliary fields and weights from measurements, record file hashes and row counts, and synchronize the base metadata's groups and partitions. Called automatically after splitting. |
| 5. Cross-validation | `code/compare_dataset1_weights.py` | Run paired five-fold grouped cross-validation within the training set, using the same folds and model settings for unweighted and weighted variants. No hyperparameter tuning is performed. |
| 6. Final training and validation | `code/train_and_validate.py` | Fit each final model on all training rows, save the fitted pipelines as `.joblib` files, and evaluate predictions on the held-out validation rows. Export MAE, MSE, RMSE, R², predictions and per-study metrics. |
| 7. Check saved-model predictions | `code/predict.py` and `run_training.py` | Load the saved models and generate predictions. The orchestrator checks agreement with the predictions saved during validation. |

## Shared implementation

| File | Role |
|---|---|
| `code/data_loading.py` | Load prepared data and matching metadata, validating integrity before combining them. |
| `code/final_pipeline.py` | Define preprocessing and the Extra Trees model pipeline. |
| `code/compare_tdd_features.py` | Provide helper functionality for structural TDD features. |
| `code/surrogate_model.py` | Provide shared helper functionality used by the implementation. |

These helpers are imported by the workflow; they are not additional sequential steps to run manually.

## Inputs, weights and outputs

The current dataset has **404 observations**, split into **288 training rows and 116 validation rows**, with **22 original input features and four prediction targets**. These counts describe the current version, not a general requirement for future datasets. The current orchestrator explicitly checks for 404 rows and must be updated if the dataset grows.

Training reads `data/prepared/train.csv` with `train_metadata.csv`. Validation reads the corresponding validation pair. `schema.json` identifies features and targets. IDs, group labels, seeds and weights are not prediction features.

- `unweighted.joblib`: fitted pipeline trained with equal weight for every training observation.
- `impact_weighted.joblib`: fitted pipeline trained with each observation's `sample_weight`.

Weights express study priorities. They do not multiply input/target values, duplicate rows, or impose a feature-importance ranking. Both ordinary and impact-weighted evaluation metrics are reported, separately from whether training used weights.

Cross-validation models are temporary evaluation models. Each final model is fitted afresh on all training rows. Validation does not update the fitted models. Preprocessing is fitted only on the training portion in each fold or final fit.

Each run stores outputs under `results/<run_id>/`, including `cross_validation/`, `final_results/`, code and prepared-data snapshots, logs, and environment information. The final-results directory contains the saved models, metrics and validation predictions.

The validation set has already been inspected during development, so it is an exploratory holdout rather than a new independent external test. The current HARQ ON/OFF study is entirely in validation because its connected group is kept together.

## Relationship to the GitHub Dataset folder

The local `data/` preparation directory corresponds to [`Dataset/`](Dataset/) in this repository. See its [README](Dataset/README.md) for the detailed data and metadata structure. This workflow documentation does not change the dataset or incorporate the experimental simulator AMC patch into training.
