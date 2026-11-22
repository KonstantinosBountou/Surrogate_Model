"""Predict all four metrics, then compare with stored independent-run measurements."""
from pathlib import Path
import argparse,json
import numpy as np
import pandas as pd
import joblib
from sklearn.metrics import mean_absolute_error,mean_squared_error,r2_score
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--results',choices=['reference','reproduced'],default='reproduced');args=p.parse_args()
res=root/'results'/args.results
schema=json.loads((root/'dataset/schema.json').read_text());inputs=schema['numeric']+schema['categorical'];cats=schema['categorical']
d=pd.read_csv(root/'test-configurations/configurations_24.csv');x=d[inputs].copy()
for c in cats:x[c]=x[c].astype(str)
slots=x.tdd_pattern.map(lambda s:s.strip('|').split('|'));x['tdd_slots']=slots.map(len);x['tdd_ul_fraction']=slots.map(lambda s:s.count('UL')/len(s));x['tdd_period_ms']=x.tdd_slots*2.**(-x.numerology);x['tdd_transitions']=slots.map(lambda s:sum(s[i]!=s[(i+1)%len(s)] for i in range(len(s))))
result=d.copy();selected=json.loads((res/'remaining_metrics/selected_models.json').read_text())
for target,name in selected.items():
 b=joblib.load(res/'remaining_metrics/models'/name);z=b['preprocessor'].transform(x) if b['preprocessor'] is not None else x
 result['predicted_'+target]=b['estimator'].predict(z)*b['target_scale']+b['target_center']
for flag in [True,False]:
 mask=d.harq_enabled.astype(str).str.lower().eq(str(flag).lower());b=joblib.load(res/'delay/models'/f'delay_harq_{flag}.joblib');z=x.loc[mask].drop(columns='tdd_slots');z=b['preprocessor'].transform(z) if b['preprocessor'] is not None else z
 y=b['estimator'].predict(z)*b['scale']+b['center']
 if b['spec']['transform']=='log1p':y=np.expm1(y)
 result.loc[mask,'predicted_delay_ms']=y
# Recompute actual means from the supplied ns-3 runs and join by ID.
runs=pd.read_csv(root/'test-configurations/ns3_runs_120.csv')
assert len(runs)==120 and runs.groupby('configuration_id').size().eq(5).all()
actual=runs.groupby('configuration_id')[schema['targets']].mean().add_prefix('actual_mean_')
assert actual.index.is_unique and set(actual.index)==set(d.configuration_id)
metrics=[]
for target in schema['targets']:
 y=actual.loc[d.configuration_id,'actual_mean_'+target].to_numpy();yp=result['predicted_'+target].to_numpy()
 assert np.isfinite(yp).all()
 result['actual_mean_'+target]=y;result['absolute_error_'+target]=abs(y-yp)
 metrics.append(dict(target=target,n=len(y),mae=mean_absolute_error(y,yp),rmse=np.sqrt(mean_squared_error(y,yp)),r2=r2_score(y,yp),negative_predictions=int((yp<0).sum())))
out=res/'comparison';out.mkdir(exist_ok=True);result.to_csv(out/'benchmark24_comparison_all_metrics.csv',index=False);pd.DataFrame(metrics).to_csv(out/'benchmark24_metrics_all.csv',index=False)
print(pd.DataFrame(metrics).to_string(index=False))
