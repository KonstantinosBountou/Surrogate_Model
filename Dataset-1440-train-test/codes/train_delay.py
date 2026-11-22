from pathlib import Path
import os
os.environ['OPENBLAS_NUM_THREADS']='2'
import json,shutil,hashlib,warnings
import numpy as np
import pandas as pd
import joblib
from catboost import CatBoostRegressor
from sklearn.ensemble import ExtraTreesRegressor
from sklearn.compose import ColumnTransformer
from sklearn.preprocessing import StandardScaler,OneHotEncoder
from sklearn.metrics import mean_absolute_error,mean_squared_error,r2_score
root=Path(__file__).resolve().parents[1];out=root/'results/reproduced/delay'
assert not (out/'verification.json').exists(),'Completed training exists'
out.mkdir(parents=True,exist_ok=True);(out/'models').mkdir(exist_ok=True)
for name in ['train_means.csv','validation_means.csv','train_metadata.csv','validation_metadata.csv','split_manifest.csv']:
 shutil.copy2(root/'results/reproduced/remaining_metrics'/name,out/name)
train=pd.read_csv(out/'train_means.csv');val=pd.read_csv(out/'validation_means.csv');schema=json.loads((root/'dataset/schema.json').read_text());inputs=schema['numeric']+schema['categorical'];cats=schema['categorical'];nums=schema['numeric']+['tdd_ul_fraction','tdd_period_ms','tdd_transitions']
def engineer(d):
 x=d[inputs].copy()
 for c in cats:x[c]=x[c].astype(str)
 slots=x.tdd_pattern.map(lambda p:p.strip('|').split('|'));x['tdd_ul_fraction']=slots.map(lambda s:s.count('UL')/len(s));x['tdd_period_ms']=slots.map(len)*2.**(-x.numerology);x['tdd_transitions']=slots.map(lambda s:sum(s[i]!=s[(i+1)%len(s)] for i in range(len(s))))
 return x
def regime(d):return d.harq_enabled.astype(str).str.lower().eq('true').to_numpy()
specs=[dict(name=f'{algo}_{transform}',algo=algo,transform=transform) for algo in ['ExtraTrees','CatBoost'] for transform in ['raw','log1p']]
def fit(d,spec):
 y=d.delay_ms.to_numpy();z=np.log1p(y) if spec['transform']=='log1p' else y;center=z.mean();scale=max(z.std(),1e-12);x=engineer(d);pre=None
 if spec['algo']=='ExtraTrees':
  pre=ColumnTransformer([('numeric',StandardScaler(),nums),('categorical',OneHotEncoder(handle_unknown='ignore',sparse_output=False),cats)]);x=pre.fit_transform(x);m=ExtraTreesRegressor(n_estimators=240,min_samples_leaf=1,max_features=1.,random_state=42,n_jobs=2)
 else:m=CatBoostRegressor(iterations=600,depth=6,learning_rate=.04,l2_leaf_reg=5,random_seed=42,thread_count=2,verbose=False,allow_writing_files=False)
 if pre is None:m.fit(x,(z-center)/scale,cat_features=cats)
 else:m.fit(x,(z-center)/scale)
 return dict(estimator=m,preprocessor=pre,center=center,scale=scale,spec=spec)
def predict(b,d):
 x=engineer(d);x=b['preprocessor'].transform(x) if b['preprocessor'] is not None else x;p=b['estimator'].predict(x)*b['scale']+b['center']
 if b['spec']['transform']=='log1p':p=np.expm1(p)
 return p
def score(y,p):return dict(mae=float(mean_absolute_error(y,p)),rmse=float(np.sqrt(mean_squared_error(y,p))),r2=float(r2_score(y,p)),negative_predictions=int((p<0).sum()))
# Input-only grouped inner splits for each HARQ regime. Each family kept together.
rng=np.random.default_rng(20261117);folds={};manifest=[]
for flag in [True,False]:
 d=train.loc[regime(train)==flag].reset_index(drop=True);x=engineer(d);constraints=[]
 for c in cats:
  for v in x[c].unique():constraints.append(set(d.loc[x[c].eq(v),'split_group']))
 for c in nums:
  for v in [x[c].min(),x[c].max()]:constraints.append(set(d.loc[x[c].eq(v),'split_group']))
 anchors=set().union(*(g for g in constraints if len(g)==1));eligible=sorted(set(d.split_group)-anchors);assert len(eligible)>=2,(flag,len(eligible))
 folds[flag]=[]
 for fold in range(3):
  for attempt in range(10000):
   held=set(rng.choice(eligible,size=max(1,round(len(eligible)*.25)),replace=False))
   if any(g<=held for g in constraints):continue
   tr=np.flatnonzero(~d.split_group.isin(held));va=np.flatnonzero(d.split_group.isin(held));break
  else:raise RuntimeError('No inner split')
  folds[flag].append((tr,va));manifest.extend(dict(harq_enabled=flag,fold=fold+1,case_id=r.base_configuration_id,partition='validation' if i in set(va) else 'train') for i,r in d.iterrows())
pd.DataFrame(manifest).to_csv(out/'inner_split_manifest.csv',index=False)
inner=[];selected={}
for flag in [True,False]:
 d=train.loc[regime(train)==flag].reset_index(drop=True)
 for s in specs:
  for fold,(tr,va) in enumerate(folds[flag],1):
   b=fit(d.iloc[tr],s);p=predict(b,d.iloc[va]);inner.append(dict(harq_enabled=flag,candidate=s['name'],fold=fold,n=len(va),**score(d.iloc[va].delay_ms,p)))
  print('INNER',flag,s['name'],flush=True)
 ranking=pd.DataFrame(inner).loc[lambda x:x.harq_enabled==flag].groupby('candidate')[['mae','rmse']].mean().sort_values(['rmse','mae']);selected[str(flag)]=ranking.index[0]
pd.DataFrame(inner).to_csv(out/'inner_metrics.csv',index=False);(out/'selection_before_outer.json').write_text(json.dumps(selected,indent=2))
models={}
for flag in [True,False]:
 s=next(s for s in specs if s['name']==selected[str(flag)]);b=fit(train.loc[regime(train)==flag],s);models[flag]=b;joblib.dump(b,out/'models'/f'delay_harq_{flag}.joblib')
def routed(d):
 p=np.empty(len(d))
 for flag in [True,False]:
  mask=regime(d)==flag
  if mask.any():p[mask]=predict(models[flag],d.loc[mask])
 return p
vp=routed(val);vrows=[]
for subset in ['all','original_endpoints','targeted_intermediate','dense_transition','delay_extension_1440']:
 mask=np.ones(len(val),bool) if subset=='all' else val.validation_scope.eq(subset).to_numpy();vrows.append(dict(subset=subset,n=int(mask.sum()),**score(val.loc[mask,'delay_ms'],vp[mask])))
val.assign(predicted_delay_ms=vp,absolute_error_delay_ms=abs(val.delay_ms-vp)).to_csv(out/'validation_predictions.csv',index=False);pd.DataFrame(vrows).to_csv(out/'validation_metrics.csv',index=False)
for flag in [True,False]:
 mask=regime(val)==flag
 np.testing.assert_allclose(predict(models[flag],val.loc[mask]),predict(joblib.load(out/'models'/f'delay_harq_{flag}.joblib'),val.loc[mask]),atol=1e-10)
(out/'verification.json').write_text(json.dumps(dict(complete=True,training_cases=len(train),validation_cases=len(val),selected=selected),indent=2))
print('DELAY COMPLETE',selected,flush=True)
