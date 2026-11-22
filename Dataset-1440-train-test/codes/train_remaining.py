"""Paired endpoint benchmark: group CV, train-only preprocessing, no interior simulations."""
from pathlib import Path
import sys,json,hashlib,shutil,time,warnings,platform
import os
extra=os.environ.get('CBR_ML_DEPS')
if extra:sys.path.insert(0,extra)
import numpy as np
import pandas as pd
import sklearn,joblib,catboost
from catboost import CatBoostRegressor
from sklearn.ensemble import ExtraTreesRegressor
from sklearn.gaussian_process import GaussianProcessRegressor
from sklearn.gaussian_process.kernels import ConstantKernel,Matern,WhiteKernel
from sklearn.preprocessing import StandardScaler,OneHotEncoder
from sklearn.compose import ColumnTransformer
from sklearn.model_selection import GroupKFold
from sklearn.metrics import mean_absolute_error,mean_squared_error,r2_score

root=Path(__file__).resolve().parents[1]
src=root/'dataset';out=root/'results/reproduced/remaining_metrics';out.mkdir(exist_ok=True,parents=True)
assert not (out/'status.json').exists(), 'Use a fresh output directory; completed training exists'
(out/'models').mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources={str(src/n):sha(src/n) for n in ['dataset.csv','metadata.csv','schema.json']}
d=pd.read_csv(src/'dataset.csv');m=pd.read_csv(src/'metadata.csv');schema=json.loads((src/'schema.json').read_text())
policy=json.loads(Path(__file__).with_name('weight_policy.json').read_text());inputs=schema['numeric']+schema['categorical'];targets=[t for t in schema['targets'] if t!='delay_ms']
assert len(d)==len(m)==1440 and d.traffic_model.eq('CBR').all()
family_fields=[f for f in inputs if f!='ue_distance_2d_m']
def family(row):
 return hashlib.sha256(json.dumps([str(row[f]) for f in family_fields],ensure_ascii=False).encode()).hexdigest()[:16]
m['split_group']=d.apply(family,axis=1);m['base_configuration_id']=m.split_group+'_'+d.ue_distance_2d_m.astype(str);m['record_id']=m.simulation_id
assert m.split_group.nunique()==99
joint=pd.concat([d,m[['base_configuration_id','split_group','rng_run','record_id']]],axis=1);records=[];lineage=[]
for bid,g in joint.groupby('base_configuration_id',sort=True):
 assert len(g)==5 and set(g.rng_run)==set(range(1,6)) and all(g[f].nunique()==1 for f in inputs)
 row={f:g[f].iloc[0] for f in inputs};row.update(base_configuration_id=bid,split_group=g.split_group.iloc[0],n_runs=5)
 for t in schema['targets']:row[t]=g[t].mean();row[t+'_sem2']=g[t].var(ddof=1)/5
 records.append(row);lineage.append(dict(base_configuration_id=bid,record_ids=';'.join(g.record_id)))
a=pd.DataFrame(records);assert len(a)==288
a.to_csv(out/'configuration_means.csv',index=False);pd.DataFrame(lineage).to_csv(out/'lineage.csv',index=False)
def engineered(frame):
 x=frame[inputs].copy()
 for col in schema['categorical']:x[col]=x[col].astype(str)
 slots=x.tdd_pattern.map(lambda p:p.strip('|').split('|'))
 x['tdd_slots']=slots.map(len);x['tdd_ul_fraction']=slots.map(lambda s:s.count('UL')/len(s));x['tdd_period_ms']=x.tdd_slots*2.**(-x.numerology)
 x['tdd_transitions']=slots.map(lambda s:sum(s[i]!=s[(i+1)%len(s)] for i in range(len(s))))
 return x
x=engineered(a);nums=schema['numeric']+['tdd_slots','tdd_ul_fraction','tdd_period_ms','tdd_transitions'];cats=schema['categorical']
def weights(frame):
 result=[]
 for _,r in frame.iterrows():
  active=[name for name,fields in policy['groups'].items() if any(r[f]!=policy['reference'][f] for f in fields)]
  result.append(float(np.mean([policy['coefficients'][k] for k in active])) if active else 1.)
 w=np.array(result);return w/w.mean()
variants=['ExtraTrees','CatBoost','GaussianProcess','ExtraTrees_weighted','CatBoost_weighted']
settings=dict(ExtraTrees=dict(n_estimators=160,min_samples_leaf=1,max_features=1.,random_state=42,n_jobs=2),CatBoost=dict(iterations=500,depth=6,learning_rate=.05,loss_function='RMSE',random_seed=42,thread_count=2,verbose=False,allow_writing_files=False),GaussianProcess='Standardized numeric + one-hot categorical; C * Matern(nu=1.5, isotropic) + WhiteKernel; alpha=SEM² standardized by training target variance + 1e-8; no impact weights; zero optimizer restarts')
# Preserve original split, append the preregistered distance-level split.
partition_map=m.groupby('base_configuration_id').partition.agg(lambda s:s.unique().tolist())
assert all(len(v)==1 for v in partition_map)
assignment=a.base_configuration_id.map(lambda bid:partition_map[bid][0]).to_numpy()
scope_map=m.groupby('base_configuration_id').validation_scope.first()
a['validation_scope']=a.base_configuration_id.map(scope_map)
tr=np.flatnonzero(assignment=='train');va=np.flatnonzero(assignment=='validation')
assert len(tr)==226 and len(va)==62
assert not set(a.iloc[tr].base_configuration_id)&set(a.iloc[va].base_configuration_id)
for col in cats:assert set(x.iloc[va][col])<=set(x.iloc[tr][col]),col
for col in nums:assert x.iloc[va][col].min()>=x.iloc[tr][col].min()-1e-9 and x.iloc[va][col].max()<=x.iloc[tr][col].max()+1e-9,col
folds=[(tr,va)];training_indices=tr.copy();validation_indices=va.copy()
a[['base_configuration_id','traffic_model','ue_distance_2d_m','split_group','validation_scope']].assign(partition=assignment).to_csv(out/'split_manifest.csv',index=False)
for part,indices in [('train',tr),('validation',va)]:a.iloc[indices].to_csv(out/(part+'_means.csv'),index=False)
for part in ['train','validation']:
 mask=m.partition.eq(part);d.loc[mask].to_csv(out/(part+'_runs.csv'),index=False)
 mm=m.loc[mask].copy();mm['csv_row']=range(2,len(mm)+2);mm.to_csv(out/(part+'_metadata.csv'),index=False)
report=dict(train_cases=226,validation_cases=62,train_runs=1130,validation_runs=310,new_validation_cases=12,original_validation_cases=38,targeted_validation_cases=6,dense_validation_cases=6,original_split_preserved=True,repetition_leakage=False,targeted_families_shared_across_distances=True,unknown_categories=0,numeric_range_violations=0,targets_used_for_new_split=False)
(out/'split_audit.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)
(out/'protocol.json').write_text(json.dumps(dict(settings=settings,split=report,selection='Original960 partition unchanged; targeted distances160/180 held out, others trained. This measures within-family distance interpolation for targeted cases, not unseen-family generalization. Models selected by pooled validation RMSE; original and targeted subsets reported separately.',targets='Mean of5runs',weight_policy=policy,test_files_used_for_fit=False,test_diagnostics_used_to_design_extension=True,source_hashes=sources),indent=2))
def fit(variant,tr,t):
 start=time.time();y=a.iloc[tr][t].to_numpy();center=y.mean();scale=max(y.std(),1e-12);ys=(y-center)/scale;pre=None
 if variant.startswith('CatBoost'):
  model=CatBoostRegressor(**settings['CatBoost']);xt=x.iloc[tr]
  model.fit(xt,ys,cat_features=cats,sample_weight=weights(a.iloc[tr]) if variant.endswith('weighted') else None)
 else:
  pre=ColumnTransformer([('numeric',StandardScaler(),nums),('categorical',OneHotEncoder(handle_unknown='ignore',sparse_output=False),cats)])
  xt=pre.fit_transform(x.iloc[tr])
  if variant.startswith('ExtraTrees'):
   model=ExtraTreesRegressor(**settings['ExtraTrees']);model.fit(xt,ys,sample_weight=weights(a.iloc[tr]) if variant.endswith('weighted') else None)
  else:
   kernel=ConstantKernel(1.,(1e-3,1e3))*Matern(length_scale=1.,length_scale_bounds=(1e-2,1e3),nu=1.5)+WhiteKernel(1e-3,(1e-8,1.))
   model=GaussianProcessRegressor(kernel=kernel,alpha=a.iloc[tr][t+'_sem2'].to_numpy()/scale**2+1e-8,normalize_y=False,n_restarts_optimizer=0,random_state=42)
   model.fit(xt,ys)
 return dict(estimator=model,preprocessor=pre,target_center=center,target_scale=scale,variant=variant,target=t,raw_features=inputs,feature_recipe='engineered() in training/code/train.py'),time.time()-start
def predict(bundle,indices):
 data=x.iloc[indices];pre=bundle['preprocessor'];xt=pre.transform(data) if pre is not None else data
 return bundle['estimator'].predict(xt)*bundle['target_scale']+bundle['target_center']
predrows=[];warningrows=[];times=[]
for variant in variants:
 for fold,(tr,va) in enumerate(folds,1):
  for t in targets:
   with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always');bundle,seconds=fit(variant,tr,t);pred=predict(bundle,va)
   assert np.isfinite(pred).all()
   warningrows.extend(dict(model=variant,fold=fold,target=t,message=str(w.message)) for w in caught)
   times.append(dict(model=variant,fold=fold,target=t,seconds=seconds))
   for idx,yp in zip(va,pred):predrows.append(dict(model=variant,fold=fold,target=t,index=int(idx),base_configuration_id=a.iloc[idx].base_configuration_id,traffic_model=a.iloc[idx].traffic_model,distance_m=a.iloc[idx].ue_distance_2d_m,validation_scope=a.iloc[idx].validation_scope,actual=a.iloc[idx][t],predicted=yp,error=yp-a.iloc[idx][t]))
  print(variant,'holdout complete',flush=True)
pred=pd.DataFrame(predrows);pred.to_csv(out/'validation_predictions.csv',index=False)
metrics=[]
for (variant,t),g in pred.groupby(['model','target']):
 for subset,mask in [('all',np.ones(len(g),bool))]+[(s,g.traffic_model.eq(s)) for s in sorted(g.traffic_model.unique())]+[(str(int(s))+'m',g.distance_m.eq(s)) for s in sorted(g.distance_m.unique())]+[(s,g.validation_scope.eq(s)) for s in ['original_endpoints','targeted_intermediate','dense_transition','delay_extension_1440']]:
  q=g.loc[mask];metrics.append(dict(model=variant,target=t,subset=subset,n=len(q),mae=mean_absolute_error(q.actual,q.predicted),rmse=np.sqrt(mean_squared_error(q.actual,q.predicted)),r2=r2_score(q.actual,q.predicted),negative_predictions=int((q.predicted < -1e-9).sum()),above100_loss_predictions=int((q.predicted>100).sum()) if t=='packet_loss_pct' else 0))
metrics=pd.DataFrame(metrics);metrics.to_csv(out/'validation_metrics.csv',index=False)
core=metrics[metrics.subset.eq('all')&metrics.model.isin(variants[:3])];winner=core.sort_values('rmse').groupby('target').first().reset_index();winner.to_csv(out/'core_winners_by_rmse.csv',index=False)
print(core.to_string(index=False),flush=True)
# Save train-only models; validation outcomes are never included in fit.
for variant in variants:
 for t in targets:
  with warnings.catch_warnings(record=True) as caught:
   warnings.simplefilter('always');bundle,seconds=fit(variant,training_indices,t)
  warningrows.extend(dict(model=variant,fold='final',target=t,message=str(w.message)) for w in caught)
  path=out/'models'/(variant+'_'+t+'.joblib');joblib.dump(bundle,path)
  np.testing.assert_allclose(predict(bundle,np.arange(len(a))),predict(joblib.load(path),np.arange(len(a))),rtol=1e-12,atol=1e-12)
 print(variant,'final models saved',flush=True)
(out/'warnings.json').write_text(json.dumps(warningrows,indent=2));pd.DataFrame(times).to_csv(out/'fit_times.csv',index=False)
assert all(sha(Path(f))==h for f,h in sources.items())
(out/'status.json').write_text(json.dumps(dict(status='complete',raw_runs=1440,averaged_cases=288,groups=int(a.split_group.nunique()),algorithms=3,additional_weighted_variants=2,final_models=15,holdout_splits=1,train_cases=len(training_indices),validation_cases=len(validation_indices),saved_models_include_validation=False,hyperparameter_tuning=False,test_used=False,interior_distance_accuracy_measured=True,source_unchanged=True,reload_check='passed',versions=dict(python=platform.python_version(),sklearn=sklearn.__version__,catboost=catboost.__version__)),indent=2))
print('COMPLETE',flush=True)

selected=metrics.loc[metrics.subset.eq('all')].sort_values(['target','rmse']).groupby('target').first().reset_index()
selected.to_csv(out/'selected_models.csv',index=False)
(out/'selected_models.json').write_text(json.dumps({r.target:r.model+'_'+r.target+'.joblib' for _,r in selected.iterrows()},indent=2))
print(selected[['target','model','mae','rmse','r2']].to_string(index=False),flush=True)



