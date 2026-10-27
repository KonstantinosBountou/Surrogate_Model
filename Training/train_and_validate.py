"""Final paired training and exploratory validation. No validation-based tuning."""
from pathlib import Path
import argparse,json,hashlib,platform
import numpy as np
import pandas as pd
from predict import load_prepared
import sklearn,joblib
from sklearn.metrics import mean_absolute_error,mean_squared_error,r2_score
from predict import build

def main():
    root=Path(__file__).resolve().parent.parent
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data',type=Path,default=root/'Dataset/prepared')
    p.add_argument('--output',type=Path,default=root/'final_results')
    a=p.parse_args()
    if a.output.exists() and any(a.output.iterdir()):raise ValueError('Choose new output folder')
    schema=json.loads((a.data/'schema.json').read_text())
    tr=load_prepared(a.data/'train.csv'); va=load_prepared(a.data/'validation.csv')
    assert tr.partition.eq('train').all() and va.partition.eq('validation').all()
    for c in ['record_id','split_group']:
        assert tr[c].notna().all() and va[c].notna().all()
        assert not set(tr[c])&set(va[c])
    assert tr.record_id.is_unique and va.record_id.is_unique
    inputs=schema['numeric']+schema['categorical'];targets=schema['targets']
    assert not set(inputs)&{'sample_weight','rng_seed','study_id','record_id','partition','split_group'}
    sig=lambda d:set(map(tuple,d[inputs].astype(str).to_numpy()))
    assert not sig(tr)&sig(va)
    y=tr[targets].to_numpy(float);actual=va[targets].to_numpy(float)
    assert np.isfinite(y).all() and np.isfinite(actual).all()
    w=tr.sample_weight.to_numpy(float); vw=va.sample_weight.to_numpy(float)
    assert np.isfinite(w).all() and (w>0).all() and np.isfinite(vw).all() and (vw>0).all()
    a.output.mkdir(parents=True,exist_ok=True)
    metrics=[];studies=[];predictions=[]
    for variant in ['unweighted','impact_weighted']:
        model=build(schema)
        options={'model__sample_weight':w} if variant=='impact_weighted' else {}
        model.fit(tr[inputs],y,**options)
        path=a.output/(variant+'.joblib')
        joblib.dump(dict(pipeline=model,raw_features=inputs,targets=targets,variant=variant),path)
        saved_hash=hashlib.sha256(path.read_bytes()).hexdigest()
        loaded=joblib.load(path)
        predicted=loaded['pipeline'].predict(va[inputs])
        np.testing.assert_allclose(predicted,model.predict(va[inputs]),rtol=1e-12,atol=1e-12)
        assert np.isfinite(predicted).all()
        assert saved_hash==hashlib.sha256(path.read_bytes()).hexdigest()
        for label,weights in [('ordinary',None),('impact_weighted',vw)]:
            for j,t in enumerate(targets):
                mse=mean_squared_error(actual[:,j],predicted[:,j],sample_weight=weights)
                metrics.append(dict(model=variant,evaluation=label,target=t,mae=mean_absolute_error(actual[:,j],predicted[:,j],sample_weight=weights),mse=mse,rmse=np.sqrt(mse),r2=r2_score(actual[:,j],predicted[:,j],sample_weight=weights)))
        z=va[['record_id','study_id','split_group','sample_weight']].copy();z['model']=variant
        for j,t in enumerate(targets):
            z[t+'_actual']=actual[:,j];z[t+'_predicted']=predicted[:,j]
            for study,indices in va.groupby('study_id').groups.items():
                error=actual[indices,j]-predicted[indices,j]
                studies.append(dict(model=variant,study_id=study,target=t,n=len(indices),mae=np.abs(error).mean(),rmse=np.sqrt(np.mean(error**2))))
        predictions.append(z)
        print(variant,'trained, saved and validated',flush=True)
    pd.DataFrame(metrics).to_csv(a.output/'metrics.csv',index=False)
    pd.DataFrame(studies).to_csv(a.output/'study_metrics.csv',index=False)
    pd.concat(predictions).to_csv(a.output/'validation_predictions.csv',index=False)
    meta=dict(train_rows=len(tr),validation_rows=len(va),settings=dict(n_estimators=160,min_samples_leaf=1,max_features=1.0,random_state=42),
        sample_weight='workbook coefficient per row, no normalization by study size',validation_used_for_fit=False,
        hashes={n:hashlib.sha256((a.data/n).read_bytes()).hexdigest() for n in ['train.csv','validation.csv','train_metadata.csv','validation_metadata.csv','metadata_manifest.json','schema.json']},
        versions=dict(python=platform.python_version(),sklearn=sklearn.__version__,numpy=np.__version__,pandas=pd.__version__,joblib=joblib.__version__),
        limitation='Previously inspected validation; exploratory, not untouched external test. Defaults are assumptions. No hyperparameter tuning in this run.')
    (a.output/'run.json').write_text(json.dumps(meta,indent=2),encoding='utf8')
    print(pd.DataFrame(metrics).to_string(index=False))
if __name__=='__main__':main()
