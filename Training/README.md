# Training workflow

```text
run_all.cmd
└── run_training.py
    ├── prepare_weights.py
    │   ├── resplit_for_coverage.py
    │   └── export_metadata.py
    ├── compare_dataset1_weights.py
    ├── train_and_validate.py
    └── predict.py
```

| Stage | Code files | What they do |
|---|---|---|
| 1. Start | `run_all.cmd` → `run_training.py` | Start and coordinate the workflow, creating a separate results directory and logs. |
| 2. Prepare | `prepare_weights.py` | Run the shared Dataset preparation: read the base CSVs, assign study coefficients as weights and construct groups. |
| 3. Split | `resplit_for_coverage.py` | Run the shared split check, keeping whole groups together in training or validation. This stage is called automatically during preparation. |
| 4. Export metadata | `export_metadata.py` | Run the shared metadata export: separate auxiliary fields and weights from measurements and record hashes. Called automatically during preparation. |
| 5. Cross-validation | `compare_dataset1_weights.py` | Run paired five-fold grouped cross-validation with and without weights, using the same folds and model settings. |
| 6. Final training and validation | `train_and_validate.py` | Fit each model on the current 288 training rows, save the `.joblib` pipelines, and evaluate on the 116 validation rows. |
| 7. Check saved models | `predict.py` and `run_training.py` | Load models and generate predictions; verify agreement with the saved validation predictions. |

The three preparation files here are entry points for the implementations in the sibling `Dataset/` directory. The Dataset preparation script calls its own splitting/export scripts automatically. This avoids maintaining duplicate preparation logic.

`predict.py` also contains the shared data-integrity checks, structural TDD features, preprocessing and Extra Trees pipeline, imported by the training scripts. No separate helper modules are required.

`run_all.cmd` uses `Training/.venv/Scripts/python.exe`. The Python environment needs the libraries listed in `Dataset/requirements.txt`. Outputs go to `Training/results/<run_id>/`; they are not uploaded. The workflow expects 404 observations and does not tune hyperparameters. Validation is an already-inspected exploratory holdout.

Models created by this layout refer to the `predict` module. Older saved models from the previous layout still require their original helper modules and should be used with their original code snapshot.
