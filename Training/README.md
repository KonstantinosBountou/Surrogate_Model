# Extra Trees training and validation

This folder contains the training implementation from the local `Surrogate_Model1` project. It uses the sibling [`Dataset/`](../Dataset/) folder for preparation and inputs. The model code is unchanged; the orchestrator's paths have been adapted to this repository layout, and it creates its log directory automatically.

```text
Surrogate_Model/
├── Dataset/                 # Base inputs, preparation scripts and prepared data
└── Training/
    ├── run_all.cmd
    ├── run_training.py
    ├── requirements.txt
    └── code/
        ├── compare_dataset1_weights.py
        ├── train_and_validate.py
        ├── predict.py
        ├── data_loading.py
        ├── final_pipeline.py
        ├── compare_tdd_features.py
        └── surrogate_model.py
```

## Run on Windows (CMD)

From this `Training` directory, using Python 3.12:

```bat
py -3.12 -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt
run_all.cmd
```

For subsequent runs, use `run_all.cmd`. It calls `run_training.py`, which performs preparation, integrity/group-overlap checks, paired five-fold grouped cross-validation, final training and validation, and saved-model prediction checks. Preparation regenerates `Dataset/prepared/` and synchronizes groups and partitions in `Dataset/dataset1_metadata.csv`.

See the [stage-by-stage workflow](../TRAINING_WORKFLOW.md) for each script's role. Start with the orchestrator; the metadata export helper expects intermediate preparation files and should not be run separately on final prepared outputs.

## Outputs

Each run creates `results/<run_id>/` with cross-validation results, final models and validation metrics, code/data snapshots, logs and environment information. `status.json` records the latest status; `logs/` contains its stage logs. Generated outputs and `.venv/` are excluded from Git.

The final models are `final_results/unweighted.joblib` and `final_results/impact_weighted.joblib`. Both use the same Extra Trees settings; the second uses study coefficients as training sample weights. The current orchestrator expects 404 observations (288 training / 116 validation in the current prepared split). It does not tune hyperparameters.

The validation set has already been inspected during development. These are exploratory results, not an untouched external test. No experimental simulator AMC changes have been incorporated into these inputs.

The repository packaging was syntax-checked; uploading these scripts did not trigger a new training run. Dependencies are copied from the local project's requirements.
