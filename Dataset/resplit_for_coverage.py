# ΣΚΟΠΟΣ: διαχωρισμός ολόκληρων ομάδων σε train/validation, χωρίς training.
# Καλείται από το prepare_weights.py πριν από το export_metadata.py.
# Χρειάζεται το ενδιάμεσο dataset.csv που ακόμη περιέχει τα metadata.
from pathlib import Path
import json
import numpy as np
import pandas as pd
from scipy.optimize import milp,Bounds,LinearConstraint
# 1. Διαδρομή εξόδου ανεξάρτητη από τον φάκελο του terminal.
data=Path(__file__).resolve().parent/'prepared'
# 2. Ανάγνωση του ενδιάμεσου dataset και αποθήκευση της προηγούμενης κάλυψης.
# Το crosstab μετρά γραμμές ανά μελέτη και partition, όχι ομάδες.
d=pd.read_csv(data/'dataset.csv')
oldcoverage=pd.crosstab(d.study_id,d.partition)
oldcoverage.to_csv(data/'previous_study_coverage.csv')
# 3. Κατάλογοι ομάδων και μελετών σε σταθερή σειρά.
# counts: πλήθος γραμμών κάθε ομάδας. presence: 1 όταν η ομάδα περιλαμβάνει
# έστω μία γραμμή της μελέτης. Μία ομάδα μπορεί να συνδέει πολλές μελέτες.
groups=sorted(d.split_group.unique()); studies=sorted(d.study_id.unique())
counts=d.groupby('split_group').size().reindex(groups).to_numpy()
presence=pd.crosstab(d.study_id,d.split_group).reindex(index=studies,columns=groups,fill_value=0).gt(0).astype(float).to_numpy()
# Binary decision per existing group. Every study in validation; retain training
# coverage wherever at least two groups exist. Optimize row count only, no targets.
# 4. Μεταβλητές βελτιστοποίησης: μία δυαδική απόφαση ανά ομάδα
# (1 = validation, 0 = train) και μία μη αρνητική μεταβλητή απόκλισης.
# Οι περιορισμοί αφορούν παρουσία ομάδων, όχι τις τιμές των στόχων.
n=len(groups); matrix=[];lower=[];upper=[]
# Κάθε μελέτη πρέπει να έχει τουλάχιστον μία ομάδα στο validation.
# Αν έχει >=2 ομάδες, τουλάχιστον μία πρέπει να μείνει στο train.
# Αν έχει μόνο μία, αυτή αναγκαστικά μπαίνει ολόκληρη στο validation.
for row in presence:
    matrix.append(np.r_[row,0]);lower.append(1);upper.append(row.sum()-1 if row.sum()>1 else 1)
# Οι δύο ανισότητες κάνουν τη βοηθητική μεταβλητή τουλάχιστον ίση με
# την απόλυτη διαφορά του αριθμού validation γραμμών από το 116.
# Το 116 είναι στόχος πλήθους, όχι υποχρεωτικό ακριβές αποτέλεσμα.
matrix.extend([np.r_[counts,-1],np.r_[-counts,-1]])
lower.extend([-np.inf,-np.inf]);upper.extend([116,-116])
# Ελαχιστοποιούμε κυρίως την απόκλιση από το 116. Μικροί συντελεστές
# με seed 42 δίνουν σταθερή προτίμηση μεταξύ κοντινών λύσεων.
# Αυτό δεν είναι τυχαίος διαχωρισμός γραμμών και δεν χρησιμοποιεί σφάλματα μοντέλου.
objective=np.r_[np.random.default_rng(42).uniform(0,1e-5,n),1.0]
# 5. Πρώτα ελέγχουμε αν μπορούμε να κρατήσουμε το υπάρχον split.
# Ο συγκεκριμένος έλεγχος απαιτεί ενιαίο partition ανά ομάδα και όλες
# τις μελέτες στο validation. Δεν ελέγχει εδώ τον στόχο 116 ή παρουσία
# κάθε μελέτης στο train· αυτά είναι περιορισμοί της νέας βελτιστοποίησης.
coverage_now=pd.crosstab(d.study_id,d.partition)
keep=(d.groupby('split_group').partition.nunique().max()==1 and 'validation' in coverage_now and coverage_now.validation.gt(0).all())
# Αν ισχύουν οι παραπάνω συνθήκες, δεν εκτελείται ο solver.
# Το status=0 εδώ δηλώνει τη διατήρηση του split, όχι επίλυση MILP.
if keep:
    selected=set(d.loc[d.partition.eq('validation'),'split_group'])
    from types import SimpleNamespace
    res=SimpleNamespace(status=0,message='Preserved previous covered split; valid after refresh')
else:
    # 6. Αλλιώς λύνουμε πρόβλημα μεικτού ακέραιου γραμμικού προγραμματισμού.
    # Οι πρώτες n μεταβλητές είναι 0/1, η τελευταία συνεχής >=0.
    # Χρονικό όριο 60 s. Αν δεν επιστραφεί λύση, σταματάμε με σφάλμα.
    # Αν επιστραφεί λύση στο χρονικό όριο, δεν σημαίνει κατ’ ανάγκη ότι
    # αποδείχθηκε βέλτιστη· καταγράφονται status και μήνυμα του solver.
    res=milp(objective,integrality=np.r_[np.ones(n),0],bounds=Bounds(np.zeros(n+1),np.r_[np.ones(n),np.inf]),constraints=LinearConstraint(np.array(matrix),lower,upper),options={'time_limit':60})
    if res.x is None:raise RuntimeError(res.message)
    selected={groups[i] for i in range(n) if res.x[i]>.5}
# 7. Αντιστοιχίζουμε όλες τις γραμμές κάθε ομάδας στο ίδιο partition.
# Ελέγχουμε ξανά ότι δεν μοιράζονται ομάδες και ότι καλύπτονται οι μελέτες.
d['partition']=np.where(d.split_group.isin(selected),'validation','train')
assert d.groupby('split_group').partition.nunique().max()==1
coverage=pd.crosstab(d.study_id,d.partition)
assert coverage.validation.gt(0).all()
# 8. Ξαναγράφουμε το πλήρες dataset και τα δύο υποσύνολα.
# Εδώ τα αρχεία περιέχουν ακόμη metadata. Το επόμενο script τα διαχωρίζει.
for name,frame in [('dataset',d),('train',d[d.partition.eq('train')]),('validation',d[d.partition.eq('validation')])]:frame.to_csv(data/(name+'.csv'),index=False,encoding='utf-8-sig')
coverage.to_csv(data/'study_coverage.csv')
# 9. Αναφορές: split_manifest συνδέει IDs, μελέτες, ομάδες και partitions.
# Το summary ενημερώνεται με τα τελικά πλήθη, τις μελέτες που λείπουν
# από το train και την κατάσταση του solver. Δεν υπολογίζονται ML metrics.
d[['record_id','study_id','split_group','partition']].to_csv(data/'split_manifest.csv',index=False)
meta=json.loads((data/'summary.json').read_text())
meta.update(partition_counts=d.partition.value_counts().to_dict(),split_method='Preserve previous covered group split when valid; otherwise optimize study coverage without targets',training_studies_absent=coverage.index[coverage.train.eq(0)].tolist(),solver_status=int(res.status),solver_message=res.message)
(data/'summary.json').write_text(json.dumps(meta,indent=2),encoding='utf8')
# Εμφανίζουμε την τελική κάλυψη και τα πλήθη για έλεγχο στο terminal.
print(coverage.to_string());print(d.partition.value_counts().to_string())
