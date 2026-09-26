# Surrogate Model

Research code for ns-3/5G-LENA experiment generation and Gaussian Process surrogate modelling, with an n8n workflow for a separate single-UE baseline campaign.

## Repository contents

| Path | Purpose |
| --- | --- |
| `pipeline.py` | Existing Python campaign runner using Latin Hypercube sampling and optional GP/validation stages. |
| `surrogate_model.py` | Existing Gaussian Process training code. |
| `validation.py` | Existing simulation-based validation code. |
| `parametric.cc`, `parametric1.cc` | Existing parametric simulation sources. |
| `default-nr-scenario.cc`, `dag_complex.json` | Existing scenario/reference files. |
| [`n8n/`](n8n/README.md) | Importable workflow, its matching single-UE scenario snapshot, baseline configuration, setup instructions and pilot results. |

## n8n demonstration

The workflow runs three UE distances (50, 100 and 150 m), preserves each configuration and result, and produces per-experiment CSVs plus a combined dataset. It uses SSH to call a local or remote ns-3 host. Credentials and machine-specific paths must be supplied after import.

See the [workflow setup](n8n/README.md), [observed results](n8n/docs/results.md) and [export review](n8n/docs/export-review.md).

The n8n data-generation workflow is **not yet integrated** with the root GP scripts: their feature schemas and physical meanings differ. In particular, the workflow varies UE power, while the root GP expects gNB power. The three-row pilot is an automation demonstration, not a trained or validated surrogate. Physics-informed models and VAE synthetic-data generation are planned work.
