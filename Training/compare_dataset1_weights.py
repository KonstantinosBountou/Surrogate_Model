"""Paired grouped CV of identical Extra Trees with and without impact weights."""
from pathlib import Path
import argparse,json
import numpy as np
import pandas as pd
from predict import load_prepared
from sklearn.compose import ColumnTransformer
from sklearn.preprocessing import OneHotEncoder
from sklearn.impute import SimpleImputer
from sklearn.pipeline import Pipeline
from sklearn.ensemble import ExtraTreesRegressor
from sklearn.multioutput import MultiOutputRegressor
from sklearn.model_selection import GroupKFold
from sklearn.metrics import mean_absolute_error,mean_squared_error,r2_score
from predict import structural

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data',type=Path,default=Path(__file__).resolve().parent.parent/'Dataset/prepared')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists() and any(a.output.iterdir()):raise ValueError('Choose empty output folder')
    schema=json.loads((a.data/'schema.json').read_text())
    d=load_prepared(a.data/'train.csv');assert d.partition.eq('train').all()
    nums,cats,targets=schema['numeric'],schema['categorical'],schema['targets']
    x=d[nums+cats].copy()
    for c in cats:x[c]=x[c].astype(str)
    extra=pd.DataFrame([structural(s,mu) for s,mu in zip(d.tdd_pattern,d.numerology)])
    x=pd.concat([x,extra],axis=1);nums=nums+list(extra.columns)
    y=d[targets].to_numpy(float);weights=d.sample_weight.to_numpy(float)
    assert np.isfinite(y).all() and (weights>0).all()
    folds=list(GroupKFold(5).split(x,groups=d.split_group))
    rows=[];preds=[];foldrows=[]
    for variant in ['unweighted','impact_weighted']:
        oof=np.zeros_like(y)
        for fold,(tr,te) in enumerate(folds,1):
            assert not set(d.iloc[tr].split_group)&set(d.iloc[te].split_group)
            pre=ColumnTransformer([('num',SimpleImputer(strategy='median',add_indicator=True,keep_empty_features=True),nums),('cat',OneHotEncoder(handle_unknown='ignore',sparse_output=False),cats)])
            model=Pipeline([('preprocess',pre),('model',MultiOutputRegressor(ExtraTreesRegressor(n_estimators=160,min_samples_leaf=1,max_features=1.0,random_state=42,n_jobs=2)))])
            options={'model__sample_weight':weights[tr]} if variant=='impact_weighted' else {}
            model.fit(x.iloc[tr],y[tr],**options);oof[te]=model.predict(x.iloc[te])
            if variant=='unweighted':foldrows.extend(dict(record_id=d.iloc[i].record_id,fold=fold,split_group=d.iloc[i].split_group) for i in te)
            print(variant,fold,'/5',flush=True)
        for label,w in [('ordinary',None),('impact_weighted',weights)]:
            for j,t in enumerate(targets):
                mse=mean_squared_error(y[:,j],oof[:,j],sample_weight=w)
                rows.append(dict(model=variant,evaluation=label,target=t,mae=mean_absolute_error(y[:,j],oof[:,j],sample_weight=w),mse=mse,rmse=np.sqrt(mse),r2=r2_score(y[:,j],oof[:,j],sample_weight=w)))
        z=d[['record_id','study_id','sample_weight']].copy();z['model']=variant
        for j,t in enumerate(targets):z[t+'_actual']=y[:,j];z[t+'_predicted']=oof[:,j]
        preds.append(z)
    a.output.mkdir(parents=True,exist_ok=True)
    pd.DataFrame(rows).to_csv(a.output/'metrics.csv',index=False)
    pd.concat(preds).to_csv(a.output/'predictions.csv',index=False)
    pd.DataFrame(foldrows).to_csv(a.output/'fold_assignments.csv',index=False)
    print(pd.DataFrame(rows).to_string(index=False))
    (a.output/'README.txt').write_text('Paired 5-fold grouped CV on training only. Same fixed Extra Trees settings in both variants. No tuning, validation evaluation or final fitted model. Impact score is per-row sample_weight, not a feature. Weights are qualitative preferences, not causal importance. Compare only within this experiment: data/features may differ from v4.\n',encoding='utf8')
if __name__=='__main__':main()
