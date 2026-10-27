"""Load data and aligned metadata; reject modified/reordered CSV files."""
from pathlib import Path
import json,hashlib
import pandas as pd
def load_prepared(path):
    path=Path(path)
    manifest=json.loads((path.parent/'metadata_manifest.json').read_text(encoding='utf-8'))[path.stem]
    metadata=path.with_name(path.stem+'_metadata.csv')
    for file,key in [(path,'data_sha256'),(metadata,'metadata_sha256')]:
        if hashlib.sha256(file.read_bytes()).hexdigest()!=manifest[key]:
            raise ValueError('Data/metadata changed. Regenerate prepared files before training: '+str(file))
    data=pd.read_csv(path);meta=pd.read_csv(metadata)
    assert len(data)==len(meta)==manifest['rows']
    assert not set(data.columns)&set(meta.columns)
    assert meta.record_id.is_unique and meta.sample_weight.gt(0).all()
    return pd.concat([data,meta],axis=1)
