"""Separate non-feature metadata without discarding training weights."""
# ΣΚΟΠΟΣ: χωρισμός μετρήσεων και βοηθητικών πεδίων, χωρίς αλλαγή βαρών.
# Καλείται ΤΕΛΕΥΤΑΙΟ από το prepare_weights.py, μετά τον διαχωρισμό.
# Δεν προορίζεται για αυτόνομη επανεκτέλεση πάνω σε ήδη χωρισμένα CSV:
# απαιτεί τα ενδιάμεσα dataset/train/validation με τα πεδία metadata μέσα τους.
from pathlib import Path
import csv,json,hashlib
# 1. Φάκελος prepared δίπλα στο script, ανεξάρτητα από το terminal.
root=Path(__file__).resolve().parent/'prepared'
# 2. Τα έξι πεδία που μεταφέρονται στα *_metadata.csv.
# Το sample_weight παραμένει διαθέσιμο για weighted fit, όχι ως feature.
# Το split_group παραμένει στα δεδομένα αλλά εξαιρείται από το schema εισόδων.
fields=['record_id','study_id','coefficient_parameter','sample_weight','partition','rng_seed']
# 3. Καταγραφή πλήθους γραμμών και SHA-256 για κάθε ζεύγος αρχείων.
manifest={}
# Επεξεργαζόμαστε το πλήρες σύνολο και τα train/validation με τον ίδιο τρόπο.
for name in ['dataset','train','validation']:
    path=root/(name+'.csv')
    # DictReader: κάθε γραμμή γίνεται λεξικό με κλειδιά τα ονόματα στηλών.
    # utf-8-sig υποστηρίζει το BOM των CSV που ανοίγουν στο Excel.
    with path.open(encoding='utf-8-sig',newline='') as f:
        reader=csv.DictReader(f);columns=reader.fieldnames;rows=list(reader)
    # Σταματάμε αν λείπει κάποιο αναμενόμενο βοηθητικό πεδίο.
    assert all(c in columns for c in fields)
    # 4. Δύο έξοδοι από τις ίδιες γραμμές και στην ίδια σειρά:
    # αρχικό όνομα για τις μετρήσεις, κατάληξη _metadata για τα έξι πεδία.
    # Δεν ταξινομούμε ούτε αφαιρούμε μετρήσεις.
    for suffix,cols in [('',[c for c in columns if c not in fields]),('_metadata',fields)]:
        target=root/(name+suffix+'.csv')
        with target.open('w',encoding='utf-8-sig',newline='') as f:
            # extrasaction=ignore γράφει μόνο τις επιλεγμένες στήλες
            # από τα πλήρη λεξικά. Δεν χάνει πεδία: τα υπόλοιπα είναι στο άλλο CSV.
            writer=csv.DictWriter(f,fieldnames=cols,extrasaction='ignore');writer.writeheader();writer.writerows(rows)
    # 5. Hash των τελικών bytes, όχι των τιμών πριν την αποθήκευση.
    # Ακόμη και αλλαγή στη σειρά ή στις αλλαγές γραμμής αλλάζει το hash.
    # Οι έλεγχοι αποδεικνύουν ακεραιότητα, όχι επιστημονική ορθότητα μετρήσεων.
    manifest[name]={'rows':len(rows),'data_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'metadata_sha256':hashlib.sha256((root/(name+'_metadata.csv')).read_bytes()).hexdigest()}
# Αποθήκευση manifest που διαβάζει ο loader πριν συνδέσει δεδομένα/metadata.
(root/'metadata_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')

# Synchronize the base metadata with the current prepared split by record_id.
import pandas as pd
base=root.parent
# 6. Ενημέρωση των βασικών metadata από το τελικό split.
# Διαβάζουμε τα δύο παραγόμενα αρχεία που μόλις γράφτηκαν στην ίδια σειρά.
mapping=pd.read_csv(base/'dataset1_metadata.csv')
meta=pd.read_csv(root/'dataset_metadata.csv')
dataset=pd.read_csv(root/'dataset.csv')
# Απαιτούμε ίσο πλήθος γραμμών και μοναδικό record_id στο παραγόμενο metadata.
assert len(meta)==len(dataset) and meta.record_id.is_unique
# 7. Συνδέουμε την ομάδα της κάθε γραμμής με το record_id και ενημερώνουμε
# τα base metadata μέσω ID, όχι με υπόθεση ότι έχουν την ίδια σειρά.
# Αν λείπει αντιστοίχιση, η map παράγει κενό που εντοπίζεται στον έλεγχο.
mapping['split_group']=mapping.record_id.map(pd.Series(dataset.split_group.to_numpy(),index=meta.record_id))
mapping['partition']=mapping.record_id.map(meta.set_index('record_id').partition)
# Επιβεβαιώνουμε ότι configuration_id, ομάδα και partition δεν είναι κενά.
# Το configuration_id διατηρείται, δεν υπολογίζεται ξανά από configuration.
assert mapping[['configuration_id','split_group','partition']].notna().all().all()
# 8. Αποθήκευση του dataset1_metadata.csv με την ενημερωμένη ομάδα/partition.
# Τα άλλα πεδία διατηρούνται. Δεν διαβάζονται παλιές πηγές ή mappings.
# Τα scripts ξαναγράφουν αρχεία σταδιακά· μετά από διακοπή χρειάζεται
# επιτυχής επανεκτέλεση όλης της προετοιμασίας πριν χρησιμοποιηθούν τα outputs.
mapping.to_csv(base/'dataset1_metadata.csv',index=False,encoding='utf-8-sig')

# configuration_id, study_id, rng_run και διαδρομές διατηρούνται από τα base metadata.
