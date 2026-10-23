from pathlib import Path
import json
import numpy as np
import pandas as pd
from scipy.optimize import milp,Bounds,LinearConstraint
data=Path(__file__).resolve().parent/'prepared'
d=pd.read_csv(data/'dataset.csv')
oldcoverage=pd.crosstab(d.study_id,d.partition)
oldcoverage.to_csv(data/'previous_study_coverage.csv')
groups=sorted(d.split_group.unique()); studies=sorted(d.study_id.unique())
counts=d.groupby('split_group').size().reindex(groups).to_numpy()
presence=pd.crosstab(d.study_id,d.split_group).reindex(index=studies,columns=groups,fill_value=0).gt(0).astype(float).to_numpy()
# Binary decision per existing group. Every study in validation; retain training
# coverage wherever at least two groups exist. Optimize row count only, no targets.
n=len(groups); matrix=[];lower=[];upper=[]
for row in presence:
    matrix.append(np.r_[row,0]);lower.append(1);upper.append(row.sum()-1 if row.sum()>1 else 1)
matrix.extend([np.r_[counts,-1],np.r_[-counts,-1]])
lower.extend([-np.inf,-np.inf]);upper.extend([116,-116])
objective=np.r_[np.random.default_rng(42).uniform(0,1e-5,n),1.0]
coverage_now=pd.crosstab(d.study_id,d.partition)
keep=(d.groupby('split_group').partition.nunique().max()==1 and 'validation' in coverage_now and coverage_now.validation.gt(0).all())
if keep:
    selected=set(d.loc[d.partition.eq('validation'),'split_group'])
    from types import SimpleNamespace
    res=SimpleNamespace(status=0,message='Preserved previous covered split; valid after refresh')
else:
    res=milp(objective,integrality=np.r_[np.ones(n),0],bounds=Bounds(np.zeros(n+1),np.r_[np.ones(n),np.inf]),constraints=LinearConstraint(np.array(matrix),lower,upper),options={'time_limit':60})
    if res.x is None:raise RuntimeError(res.message)
    selected={groups[i] for i in range(n) if res.x[i]>.5}
d['partition']=np.where(d.split_group.isin(selected),'validation','train')
assert d.groupby('split_group').partition.nunique().max()==1
coverage=pd.crosstab(d.study_id,d.partition)
assert coverage.validation.gt(0).all()
for name,frame in [('dataset',d),('train',d[d.partition.eq('train')]),('validation',d[d.partition.eq('validation')])]:frame.to_csv(data/(name+'.csv'),index=False,encoding='utf-8-sig')
coverage.to_csv(data/'study_coverage.csv')
d[['record_id','study_id','split_group','partition']].to_csv(data/'split_manifest.csv',index=False)
meta=json.loads((data/'summary.json').read_text())
meta.update(partition_counts=d.partition.value_counts().to_dict(),split_method='Preserve previous covered group split when valid; otherwise optimize study coverage without targets',training_studies_absent=coverage.index[coverage.train.eq(0)].tolist(),solver_status=int(res.status),solver_message=res.message)
(data/'summary.json').write_text(json.dumps(meta,indent=2),encoding='utf8')
print(coverage.to_string());print(d.partition.value_counts().to_string())
