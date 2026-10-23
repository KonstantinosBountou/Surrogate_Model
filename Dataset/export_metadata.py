"""Separate non-feature metadata without discarding training weights."""
from pathlib import Path
import csv,json,hashlib
root=Path(__file__).resolve().parent/'prepared'
fields=['record_id','study_id','coefficient_parameter','sample_weight','partition','rng_seed']
manifest={}
for name in ['dataset','train','validation']:
    path=root/(name+'.csv')
    with path.open(encoding='utf-8-sig',newline='') as f:
        reader=csv.DictReader(f);columns=reader.fieldnames;rows=list(reader)
    assert all(c in columns for c in fields)
    for suffix,cols in [('',[c for c in columns if c not in fields]),('_metadata',fields)]:
        target=root/(name+suffix+'.csv')
        with target.open('w',encoding='utf-8-sig',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=cols,extrasaction='ignore');writer.writeheader();writer.writerows(rows)
    manifest[name]={'rows':len(rows),'data_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'metadata_sha256':hashlib.sha256((root/(name+'_metadata.csv')).read_bytes()).hexdigest()}
(root/'metadata_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')

# Synchronize the base metadata with the current prepared split by record_id.
import pandas as pd
base=root.parent
mapping=pd.read_csv(base/'dataset1_metadata.csv')
meta=pd.read_csv(root/'dataset_metadata.csv')
dataset=pd.read_csv(root/'dataset.csv')
assert len(meta)==len(dataset) and meta.record_id.is_unique
mapping['split_group']=mapping.record_id.map(pd.Series(dataset.split_group.to_numpy(),index=meta.record_id))
mapping['partition']=mapping.record_id.map(meta.set_index('record_id').partition)
assert mapping[['configuration_id','split_group','partition']].notna().all().all()
mapping.to_csv(base/'dataset1_metadata.csv',index=False,encoding='utf-8-sig')

# configuration_id, study_id, rng_run και διαδρομές διατηρούνται από τα base metadata.
