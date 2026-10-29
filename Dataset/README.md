# Dataset and sample weight preparation

This folder contains the base data and preparation scripts. It does not train models or generate predictions.

## Base sources

| File | Purpose |
|---|---|
| `dataset1.csv` | 404 observations with 26 columns: 22 input features and 4 targets. |
| `dataset1_metadata.csv` | 10 fields: `csv_row`, `record_id`, `rng_seed`, `configuration_id`, `split_group`, `partition`, `study_id`, `rng_run`, `config_path`, `results_path`. |
| `sources/impact_coefficients.xlsx` | The only file in `sources`. Provides coefficients from the `Metrics` worksheet. |

Measurements and provenance are read exclusively from the two base CSV files.

## Workflow sequence

1. **`prepare_weights.py`** validates the data, assigns study weights from Excel, merges groups with identical inputs, and exports intermediate files and reports.
2. **`resplit_for_coverage.py`** runs automatically, adjusting the split only when needed to keep groups intact and ensure study coverage, targeting about 116 validation rows without using targets or model performance.
3. **`export_metadata.py`** runs automatically, separates metadata from measurements, records file hashes, and updates group and partition assignments while preserving other metadata.

The first script is the entry point. The other scripts depend on intermediate files generated in this order.

## Sample weights

Each observation receives its study coefficient as `sample_weight`. Rows are not duplicated, inputs and targets are not multiplied, and weights are not normalized by the number of observations in a study.

Weights are stored in the metadata files under `prepared` and can subsequently be passed to the model's `fit` method during weighted training. They are not prediction features. The coefficients express priorities, not established causal feature importance.

## Generated files in prepared

| Files | Contents |
|---|---|
| `dataset.csv` and `dataset_metadata.csv` | All 404 observations and their metadata. |
| `train.csv` and `train_metadata.csv` | 288 training observations and their metadata. |
| `validation.csv` and `validation_metadata.csv` | 116 validation observations and their metadata. |
| `schema.json` | Numeric features, categorical features, and targets. |
| `metadata_manifest.json` | Data and metadata CSV hashes and row counts. |
| `weight_mapping.csv` | Observation count, weight per row, and total weight per study. |
| `split_manifest.csv` | Record ID, study, group, and partition assignments. |
| `study_coverage.csv` | Study coverage after splitting. |
| `previous_study_coverage.csv` | Study coverage before the most recent split check. |
| `summary.json` | Counts, source hashes, weighting policy, and split information. |

The data CSV files have 27 columns: the 26 columns from `dataset1` plus `split_group`. The latter is excluded from model features.

Their metadata files contain six fields: `record_id`, `study_id`, `coefficient_parameter`, `sample_weight`, `partition`, and `rng_seed`. Their structure differs from the 10 fields in the base `dataset1_metadata.csv`.

The current version contains 69 groups and 17 studies. The HARQ ON/OFF study has only one group and is assigned entirely to validation. These counts describe the current version and may change when new data are added.

## Row alignment

`csv_row` includes the header: a value of 2 identifies the first observation. Keep the two base CSV files aligned. Row numbering alone cannot detect an independent reordering of values in `dataset1`, because that file does not contain `record_id`.

For prepared files, `metadata_manifest.json` records hashes so that subsequent training code can verify file integrity before joining data and metadata. Training code is not part of this folder. Do not independently edit the generated CSV files; update the base sources and regenerate `prepared`.

## Field guides

`dataset1_parameters.docx` and `dataset1_metadata_parameters.docx` explain the base fields. The two corresponding guides under `prepared` explain the generated fields. These documents are not used by the scripts.

## Structure

```text
Dataset/
  dataset1.csv
  dataset1_metadata.csv
  prepare_weights.py
  resplit_for_coverage.py
  export_metadata.py
  requirements.txt
  README.md
  dataset1_parameters.docx
  dataset1_metadata_parameters.docx
  sources/
    impact_coefficients.xlsx
  prepared/
    dataset.csv, dataset_metadata.csv
    train.csv, train_metadata.csv
    validation.csv, validation_metadata.csv
    schema.json, metadata_manifest.json
    weight_mapping.csv, split_manifest.csv
    study_coverage.csv, previous_study_coverage.csv, summary.json
    dataset_parameters.docx, dataset_metadata_parameters.docx
```
