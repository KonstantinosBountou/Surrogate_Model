"""Run the corresponding preparation stage from the shared Dataset folder."""
from pathlib import Path
import runpy

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).resolve().parent.parent / "Dataset" / 'prepare_weights.py'), run_name="__main__")
