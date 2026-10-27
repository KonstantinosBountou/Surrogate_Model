"""Run preparation, leakage checks, paired CV and final training in this folder."""
from pathlib import Path
import sys, subprocess, json, hashlib, datetime, shutil

ROOT = Path(__file__).resolve().parent
DATA = ROOT.parent / 'Dataset'
sys.path.insert(0, str(ROOT))

def stage(name, script, *args):
    print(name, flush=True)
    with (ROOT/'logs'/f'{name}.log').open('w', encoding='utf-8') as log:
        p = subprocess.run([sys.executable, '-X', 'utf8', str(ROOT/script), *args],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError(f'{name} failed; see logs/{name}.log (exit {p.returncode})')

def main():
    (ROOT/'logs').mkdir(exist_ok=True)
    run_id=datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    out=ROOT/'results'/run_id
    out.mkdir(parents=True, exist_ok=False)
    status={'run_id':run_id,'status':'running','python':sys.executable}
    status_path=ROOT/'status.json'
    def save(): status_path.write_text(json.dumps(status,indent=2,ensure_ascii=False),encoding='utf-8')
    save()
    try:
        stage('01_prepare', 'prepare_weights.py')
        # Keep the exact training inputs for each run, even after future rebuilds.
        shutil.copytree(DATA/'prepared',out/'prepared_snapshot')
        (out/'code_snapshot').mkdir()
        for source in ROOT.glob('*.py'):
            shutil.copy2(source,out/'code_snapshot'/source.name)
        import pandas as pd
        import numpy as np
        from predict import load_prepared
        tr=load_prepared(DATA/'prepared/train.csv')
        va=load_prepared(DATA/'prepared/validation.csv')
        schema=json.loads((DATA/'prepared/schema.json').read_text())
        features=schema['numeric']+schema['categorical']
        assert len(tr)+len(va)==404
        assert not set(tr.record_id)&set(va.record_id)
        assert not set(tr.split_group)&set(va.split_group)
        signature=lambda x:set(map(tuple,x[features].astype(str).to_numpy()))
        assert not signature(tr)&signature(va)
        assert not set(features)&{'rng_seed','sample_weight','record_id','study_id','partition','split_group','simulator_regime'}
        assert np.isfinite(tr[schema['targets']].to_numpy(float)).all()
        assert np.isfinite(va[schema['targets']].to_numpy(float)).all()
        audit={'train_rows':len(tr),'validation_rows':len(va),'groups':pd.concat([tr,va]).split_group.nunique(),
               'feature_count':len(features),'targets':schema['targets'],'overlap':0,
               'training_studies_absent':sorted(set(va.study_id)-set(tr.study_id)),
               'sha256':{str(p.relative_to(ROOT.parent)):hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (DATA/'prepared').glob('*') if p.is_file()}}
        (out/'data_audit.json').write_text(json.dumps(audit,indent=2),encoding='utf-8')
        print(json.dumps(audit,indent=2),flush=True)
        stage('02_cross_validation','compare_dataset1_weights.py','--data',str(DATA/'prepared'),'--output',str(out/'cross_validation'))
        stage('03_train_validation','train_and_validate.py','--data',str(DATA/'prepared'),'--output',str(out/'final_results'))
        # Exercise the public prediction script against the saved models.
        for variant in ['unweighted','impact_weighted']:
            stage('04_predict_'+variant,'predict.py','--model',str(out/'final_results'/f'{variant}.joblib'),
                  '--input',str(DATA/'prepared/validation.csv'),'--output',str(out/f'{variant}_prediction_check.csv'))
            check=pd.read_csv(out/f'{variant}_prediction_check.csv')
            saved=pd.read_csv(out/'final_results/validation_predictions.csv')
            expected=saved[saved.model.eq(variant)][[t+'_predicted' for t in schema['targets']]]
            np.testing.assert_allclose(check.to_numpy(),expected.to_numpy(),rtol=1e-10,atol=1e-10)
        status.update(status='complete',results=str(out.relative_to(ROOT)),data_audit=audit)
    except Exception as e:
        status.update(status='failed',error=str(e));save()
        shutil.copytree(ROOT/'logs',out/'logs',dirs_exist_ok=True)
        (out/'status.json').write_text(json.dumps(status,indent=2,ensure_ascii=False),encoding='utf-8')
        raise
    save()
    with (out/'environment.txt').open('w',encoding='utf-8') as f:
        subprocess.run([sys.executable,'-m','pip','freeze'],stdout=f,check=True)
    shutil.copytree(ROOT/'logs',out/'logs',dirs_exist_ok=True)
    (out/'status.json').write_text(json.dumps(status,indent=2,ensure_ascii=False),encoding='utf-8')
    print('COMPLETE '+str(out),flush=True)

if __name__=='__main__': main()
