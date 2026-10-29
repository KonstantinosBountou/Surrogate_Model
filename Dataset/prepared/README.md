# Prepared dataset

This folder contains generated data, metadata, and reports ready for surrogate model training and validation.

| Files | Purpose |
|---|---|
| `dataset.csv`, `train.csv`, `validation.csv` | Full dataset and training/validation splits. |
| `*_metadata.csv` | Row-aligned record IDs, study information, and sample weights. |
| `schema.json` | Input features and prediction targets. |
| `metadata_manifest.json` | File hashes and row counts for integrity checks. |
| `weight_mapping.csv`, `split_manifest.csv`, `*study_coverage.csv`, `summary.json` | Weighting, group assignments, study coverage, and preparation summaries. |
| `*_parameters.docx` | Guides to data and metadata fields. |

Exclude `split_group` from model inputs. Pass `sample_weight` from the matching metadata file to the model's `fit` method for weighted training; it is not a prediction feature.

Do not edit generated CSV files directly. Update the base sources in `Dataset` and rerun `prepare_weights.py` to regenerate them.
