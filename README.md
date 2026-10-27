# Surrogate Model

Thesis project for automating ns-3/5G-LENA simulations and developing surrogate models from the results.

## n8n workflow

The workflow creates experiment configurations, runs simulations through SSH, checks for errors and saves the results as CSV files.

```mermaid
flowchart LR
    A[Generate experiments] --> B[Create configuration]
    B --> C[Run ns-3 simulation]
    C --> D[Read results]
    D --> E[Save CSV]
    E --> F{More experiments?}
    F -->|Yes| B
    F -->|No| G[Merge CSV files]
```

[View or download the workflow JSON](n8n/workflows/simulation-pipeline.json) · [Setup instructions](n8n/README.md)

The initial workflow runs three distances: **50, 100 and 150 m**, with UE power **10 dBm** and offered load **20 Mbps**. Each experiment has its own configuration, results and CSV file.

## Current progress

The dataset preparation and Extra Trees training workflow are documented in [Training workflow](TRAINING_WORKFLOW.md), including the execution tree and the responsibility of each script. Preparation files are available in [Dataset](Dataset/README.md); training scripts and execution instructions are in [Training](Training/README.md).

- Automated simulation and CSV generation are working.
- Older Python scripts for Gaussian Process training and validation are included separately at the repository root; they are distinct from the latest local Extra Trees workflow.
- Physics-informed models and VAE synthetic-data generation are planned next stages.
