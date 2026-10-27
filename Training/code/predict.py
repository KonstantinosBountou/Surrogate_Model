from pathlib import Path
import argparse,joblib,pandas as pd

if __name__=='__main__':
    p=argparse.ArgumentParser(description='Predict from raw dataset1-format inputs; no fitting.')
    p.add_argument('--model',type=Path,required=True)
    p.add_argument('--input',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Output already exists')
    b=joblib.load(a.model);d=pd.read_csv(a.input)
    pred=b['pipeline'].predict(d[b['raw_features']])
    out=pd.DataFrame(pred,columns=[t+'_predicted' for t in b['targets']])
    if 'record_id' in d:out.insert(0,'record_id',d.record_id.to_numpy())
    out.to_csv(a.output,index=False)
