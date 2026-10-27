"""Train the selected Extra Trees surrogate directly from train.csv.

Python 3.12. Install dependencies:
    pip install numpy pandas scikit-learn==1.9.1 joblib

Run (put train.csv beside this script):
    python surrogate_model.py
Optional independent validation:
    python surrogate_model.py --validation validation.csv

Uses the original selected model and hyperparameters. Grouped CV is performed
only on the training data. Missing inputs are imputed, not recovered facts.
This is an exploratory model of incompletely recorded, mixed-history campaigns.
"""
# 1. Βιβλιοθήκες και φάκελος του script.
# argparse: ορίσματα από CMD. Path: διαδρομές αρχείων.
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent
# Αν υπάρχει python-libs, οι βιβλιοθήκες του προηγούνται εκείνων του περιβάλλοντος.
if (ROOT / 'python-libs').is_dir():
    sys.path.insert(0, str(ROOT / 'python-libs'))

# joblib: αποθήκευση μοντέλου, NumPy: αριθμητικοί πίνακες,
# pandas: ανάγνωση/εγγραφή CSV, sklearn: εκπαίδευση και αξιολόγηση.
import joblib
import numpy as np
import pandas as pd
from sklearn.base import clone
from sklearn.compose import ColumnTransformer
from sklearn.ensemble import ExtraTreesRegressor
from sklearn.impute import SimpleImputer
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
from sklearn.model_selection import GroupKFold
from sklearn.multioutput import MultiOutputRegressor
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import OneHotEncoder

# 2. Είσοδοι του μοντέλου: αριθμητικές παράμετροι και κατηγορίες.
# Μόνο αυτές οι στήλες χρησιμοποιούνται για πρόβλεψη.
# IDs, seed/run και μετρημένα αποτελέσματα δεν είναι χαρακτηριστικά εισόδου.
NUMERIC = [
    'ue_distance_2d_m', 'ue_tx_power_dbm', 'offered_load_mbps', 'bandwidth_hz',
    'numerology', 'packet_size_bytes', 'rlc_buffer_size_bytes', 'carrier_frequency_hz',
    'gnb_antenna_max_gain_dbi', 'gnb_antenna_orientation_deg',
    'gnb_antenna_horizontal_beamwidth_deg', 'gnb_antenna_vertical_beamwidth_deg',
    'harq_max_retransmissions', 'harq_processes', 'packet_interval_us',
    'on_time_ms', 'off_time_ms', 'on_data_rate_bps', 'nominal_duty_cycle',
]
# Οι κατηγορίες κωδικοποιούνται αργότερα σε αριθμητικές στήλες.
CATEGORICAL = [
    'tdd_pattern', 'scheduler_type', 'rlc_mode', 'harq_enabled',
    'gnb_antenna_model', 'traffic_model', 'traffic_input_mode',
]
# 3. Στόχοι: οι τέσσερις πραγματικές μετρήσεις που μαθαίνουμε να προβλέπουμε.
# x = παράμετροι εισόδου, y = γνωστές τιμές στόχων.
# Το packet_loss_pct είναι ποσοστό 0–100, όχι κλάσμα 0–1.
TARGETS = ['throughput_mbps', 'delay_ms', 'jitter_ms', 'packet_loss_pct']


def prepare_features(df):
    """IDs, RNG, provenance and measured outcomes never enter the model."""
    # 4. Επιλέγουμε τις εισόδους χωρίς να αλλάζουμε τον αρχικό πίνακα.
    # Εδώ ελέγχονται τύποι δεδομένων· δεν γίνεται εκπαίδευση.
    x = df[NUMERIC + CATEGORICAL].copy()
    for col in NUMERIC:
        # Μη έγκυρο αριθμητικό κείμενο προκαλεί σφάλμα, δεν αγνοείται σιωπηρά.
        x[col] = pd.to_numeric(x[col], errors='raise')
    for col in CATEGORICAL:
        # Οι άγνωστες/μη καταγεγραμμένες τιμές αποκτούν ρητή κατηγορία.
        x[col] = x[col].map(lambda v: str(v) if pd.notna(v) else '__UNRECORDED__')
    return x


def make_model():
    # 5. Διαφορετική προεπεξεργασία για αριθμητικές και κατηγορικές εισόδους.
    # Η διάμεσος υπολογίζεται μόνο στα δεδομένα του αντίστοιχου fit.
    # add_indicator: δείκτες για στήλες με ελλείψεις κατά το fit.
    # keep_empty_features: κρατά εντελώς κενές στήλες, συμπληρώνοντάς τες με 0.
    # Η συμπλήρωση είναι στατιστικός χειρισμός, όχι ανάκτηση πραγματικών ρυθμίσεων.
    # OneHotEncoder: μία στήλη ένδειξης ανά μαθημένη κατηγορία.
    # Άγνωστη κατηγορία στο predict δίνει μηδενικά στο αντίστοιχο τμήμα.
    preprocessing = ColumnTransformer([
        ('numeric', SimpleImputer(strategy='median', add_indicator=True,
                                  keep_empty_features=True), NUMERIC),
        ('categorical', OneHotEncoder(handle_unknown='ignore',
                                      sparse_output=False), CATEGORICAL),
    ])
    # 6. Τέσσερις ανεξάρτητοι regressors, με 160 δέντρα ο καθένας.
    # Κάθε τελικό φύλλο έχει τουλάχιστον 2 δείγματα εκπαίδευσης.
    # random_state: αναπαραγώγιμη τυχαιότητα ML, ανεξάρτητη από το seed του ns-3.
    # n_jobs=2: έως δύο παράλληλες εργασίες για τα δέντρα κάθε regressor.
    # Οι ρυθμίσεις είναι σταθερές· εδώ δεν γίνεται επιλογή αλγορίθμου ή tuning.
    regressor = MultiOutputRegressor(ExtraTreesRegressor(
        n_estimators=160, min_samples_leaf=2, random_state=42, n_jobs=2))
    # 7. Pipeline = προεπεξεργασία και μοντέλο σε μία ενιαία ακολουθία.
    # fit: μαθαίνει τους μετασχηματισμούς και τα δέντρα από το training.
    # predict: εφαρμόζει όσα έμαθε, χωρίς επανεκπαίδευση στο validation.
    return Pipeline([('preprocess', preprocessing), ('model', regressor)])


def metric_rows(actual, predicted, evaluation):
    # Βοηθητική συνάρτηση του βήματος 10: μία γραμμή μετρικών ανά στόχο.
    # MAE: μέσο απόλυτο σφάλμα. RMSE: δίνει μεγαλύτερη βαρύτητα σε μεγάλες αποκλίσεις.
    # MAE/RMSE έχουν τις μονάδες του στόχου και ιδανικά είναι 0.
    # R²: ιδανικά 1, δεν είναι ποσοστό ακρίβειας. Μπορεί να γίνει αρνητικό.
    # Για πρακτικά σταθερές πραγματικές τιμές αφήνουμε το R² κενό (NaN).
    return [dict(evaluation=evaluation, target=target,
                 mae=mean_absolute_error(actual[:, i], predicted[:, i]),
                 rmse=np.sqrt(mean_squared_error(actual[:, i], predicted[:, i])),
                 r2=r2_score(actual[:, i], predicted[:, i])
                 if np.ptp(actual[:, i]) > 1e-12 else np.nan)
            for i, target in enumerate(TARGETS)]


def main():
    # 8. Διαβάζουμε τις διαδρομές από την εντολή του CMD.
    # --validation είναι προαιρετικό· --output ορίζει πού γράφονται τα αποτελέσματα.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--train', type=Path, default=ROOT / 'train.csv')
    parser.add_argument('--validation', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'surrogate_output')
    args = parser.parse_args()
    df = pd.read_csv(args.train)
    # Χωρίζουμε τον πίνακα σε εισόδους x και τέσσερις στόχους y.
    # Στο αρχικό train.csv: 298 γραμμές, 27 είσοδοι πριν από το one-hot, 4 στόχοι.
    x, y = prepare_features(df), df[TARGETS].to_numpy(float)
    # Δεν επινοούμε στόχους: κενές ή άπειρες μετρήσεις προκαλούν διακοπή.
    if not np.isfinite(y).all():
        raise ValueError('Targets must be finite. Missing targets are not imputed.')
    # Το 5-fold grouped CV απαιτεί τουλάχιστον πέντε διαφορετικές ομάδες.
    if df.split_group.nunique() < 5:
        raise ValueError('At least five independent split groups are required.')
    model = make_model()
    # 9. Out-of-fold προβλέψεις: κάθε γραμμή προβλέπεται ενώ η ομάδα της
    # βρίσκεται εκτός της αντίστοιχης εκπαίδευσης. Ο πίνακας συμπληρώνεται σταδιακά.
    oof = np.zeros_like(y)
    for train_idx, val_idx in GroupKFold(n_splits=5).split(x, y, df.split_group):
        # Το GroupKFold κρατά όλες τις γραμμές του ίδιου split_group μαζί.
        # Νέο ανεκπαίδευτο pipeline σε κάθε fold, χωρίς μεταφορά προηγούμενης μάθησης.
        fold_model = clone(model)
        fold_model.fit(x.iloc[train_idx], y[train_idx])
        # Προβλέπουμε το κρατημένο fold και αποθηκεύουμε στις αρχικές θέσεις γραμμών.
        oof[val_idx] = fold_model.predict(x.iloc[val_idx])
    # 10. Αξιολογούμε συνολικά τις out-of-fold προβλέψεις των πέντε προσωρινών μοντέλων.
    metrics = metric_rows(y, oof, 'training_grouped_cross_validation')
    # Εκπαιδεύουμε τώρα το τελικό μοντέλο σε ΟΛΟ το training (298 αρχικές γραμμές).
    # Δεν αποθηκεύουμε κάποιο από τα πέντε προσωρινά μοντέλα cross-validation.
    model.fit(x, y)
    args.output.mkdir(parents=True, exist_ok=True)
    # 11. Αποθηκεύουμε το εκπαιδευμένο pipeline και τις λίστες εισόδων/στόχων.
    # Η αποθήκευση προηγείται του validation: αν αυτό αποτύχει, το μοντέλο υπάρχει ήδη.
    joblib.dump(dict(pipeline=model, numeric_features=NUMERIC,
                     categorical_features=CATEGORICAL, targets=TARGETS),
                args.output / 'surrogate.joblib')

    if args.validation:
        # Ανεξάρτητη αξιολόγηση μόνο αν δόθηκε αρχείο --validation.
        validation = pd.read_csv(args.validation)
        # Σταματάμε αν κοινή εγγραφή, configuration ή ομάδα εμφανίζεται και στα δύο σύνολα.
        for column in ['record_id', 'configuration_id', 'split_group']:
            if set(df[column]) & set(validation[column]):
                raise ValueError(f'Training/validation overlap: {column}')
        # Πρόσθετος συντηρητικός έλεγχος για ίδια αποτελέσματα με ίδιο seed/run.
        signature_columns = ['rng_seed', 'rng_run'] + TARGETS
        if set(map(tuple, df[signature_columns].to_numpy())) & set(
                map(tuple, validation[signature_columns].to_numpy())):
            raise ValueError('Matching RNG and target vector crosses the split.')
        actual = validation[TARGETS].to_numpy(float)
        if not np.isfinite(actual).all():
            raise ValueError('Validation targets must be finite.')
        # Μόνο predict: οι πραγματικοί στόχοι validation δεν εκπαιδεύουν το μοντέλο.
        predicted = model.predict(prepare_features(validation))
        metrics += metric_rows(actual, predicted, 'independent_validation')
        # 12. Ανά εγγραφή κρατάμε ID, πραγματικές τιμές και προβλέψεις των 4 στόχων.
        predictions = validation[['record_id']].copy()
        for i, target in enumerate(TARGETS):
            predictions[target + '_actual'] = actual[:, i]
            predictions[target + '_predicted'] = predicted[:, i]
        predictions.to_csv(args.output / 'validation_predictions.csv', index=False)
    # Συνοπτικό CSV: 4 γραμμές cross-validation και άλλες 4 αν δόθηκε validation.
    # Τα αρχεία γράφονται στον --output, μαζί με το ήδη αποθηκευμένο surrogate.joblib.
    metrics = pd.DataFrame(metrics)
    metrics.to_csv(args.output / 'metrics.csv', index=False)
    # Εμφανίζουμε τις ίδιες μετρικές και τη θέση του μοντέλου στο terminal.
    print(metrics.to_string(index=False))
    print(f'Trained on {len(df)} rows. Saved model: {args.output / "surrogate.joblib"}')


if __name__ == '__main__':
    # Εκκίνηση όταν τρέχουμε το αρχείο από CMD. Δεν εκτελείται προσομοίωση ns-3:
    # η εκπαίδευση και η αξιολόγηση χρησιμοποιούν μόνο τις υπάρχουσες μετρήσεις CSV.
    main()
