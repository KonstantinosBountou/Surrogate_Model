# Dataset και προετοιμασία βαρών

Ο φάκελος Dataset είναι το αντίγραφο του τοπικού φακέλου data. Περιέχει τα βασικά δεδομένα και τα scripts προετοιμασίας. Δεν εκτελεί εκπαίδευση μοντέλου ούτε παράγει προβλέψεις.

## Βασικές πηγές

| Αρχείο | Ρόλος |
|---|---|
| dataset1.csv | 404 μετρήσεις με 26 στήλες: 22 είσοδοι και 4 στόχοι. |
| dataset1_metadata.csv | 10 πεδία: csv_row, record_id, rng_seed, configuration_id, split_group, partition, study_id, rng_run, config_path, results_path. |
| sources/impact_coefficients.xlsx | Μοναδικό αρχείο στον sources. Πηγή coefficients από το φύλλο Metrics. |

Οι μετρήσεις και η προέλευσή τους διαβάζονται αποκλειστικά από τα δύο βασικά CSV. Δεν διαβάζονται original_dataset.csv, row_mapping.csv, default_assumptions.csv ή verified_run_provenance.csv. Δεν συμπληρώνονται νέα defaults ούτε γίνεται σύγκριση των τιμών με τις ιστορικές πηγές.

Οι παλιές πηγές δεν απαιτούνται και δεν περιλαμβάνονται σε αυτόν τον φάκελο. Οι υπάρχουσες συμπληρώσεις defaults στο dataset παραμένουν υποθέσεις για τις ιστορικές ρυθμίσεις.

## Σειρά εκτέλεσης

1. **prepare_weights.py**: ελέγχει αντιστοίχιση γραμμών, μοναδικά IDs, πληρότητα και τύπους. Χρησιμοποιεί το study_id για να αποδώσει το βάρος από το Excel. Διατηρεί τις ομάδες των metadata και συγχωνεύει ομάδες με ίδιες εισόδους. Γράφει ενδιάμεσα δεδομένα, schema και αναφορές.
2. **resplit_for_coverage.py**: καλείται αυτόματα. Διατηρεί τον διαχωρισμό αν καμία ομάδα δεν μοιράζεται σε train/validation και όλες οι μελέτες υπάρχουν στο validation. Αλλιώς υπολογίζει νέο διαχωρισμό ολόκληρων ομάδων, με στόχο περίπου 116 γραμμές validation και παρουσία στο train όπου υπάρχουν τουλάχιστον δύο ομάδες ανά μελέτη. Δεν χρησιμοποιεί στόχους ή επιδόσεις μοντέλου για την επιλογή του split.
3. **export_metadata.py**: καλείται αυτόματα. Χωρίζει τα βοηθητικά πεδία από τις μετρήσεις, γράφει hashes και ενημερώνει τα split_group και partition στο dataset1_metadata.csv, διατηρώντας τα υπόλοιπα πεδία του.

Εκτελούμε μόνο το πρώτο script. Τα υπόλοιπα χρειάζονται τα ενδιάμεσα αρχεία που παράγονται με αυτή τη σειρά.

## Βάρη

Κάθε γραμμή παίρνει ως sample_weight τον coefficient της μελέτης της. Δεν αντιγράφονται γραμμές, δεν πολλαπλασιάζονται είσοδοι ή στόχοι και δεν γίνεται κανονικοποίηση ως προς το πλήθος μετρήσεων της μελέτης.

Τα βάρη αποθηκεύονται στα metadata του prepared και περνούν αργότερα στο fit της σταθμισμένης εκπαίδευσης. Δεν είναι χαρακτηριστικά πρόβλεψης. Οι coefficients εκφράζουν προτεραιότητες, όχι αποδεδειγμένη αιτιώδη σημασία.

## Παραγόμενα αρχεία στο prepared

| Αρχεία | Περιεχόμενο |
|---|---|
| dataset.csv και dataset_metadata.csv | Όλες οι 404 μετρήσεις και τα metadata τους. |
| train.csv και train_metadata.csv | 288 μετρήσεις εκπαίδευσης και τα metadata τους. |
| validation.csv και validation_metadata.csv | 116 μετρήσεις αξιολόγησης και τα metadata τους. |
| schema.json | Αριθμητικές/κατηγορικές είσοδοι και στόχοι. |
| metadata_manifest.json | Hashes των CSV δεδομένων/metadata και πλήθη γραμμών. |
| weight_mapping.csv | Πλήθος μετρήσεων, βάρος ανά γραμμή και συνολικό βάρος ανά μελέτη. |
| split_manifest.csv | Αντιστοίχιση ID, μελέτης, ομάδας και partition. |
| study_coverage.csv | Κάλυψη μελετών μετά τον διαχωρισμό. |
| previous_study_coverage.csv | Κάλυψη πριν από τον τελευταίο έλεγχο διαχωρισμού. |
| summary.json | Πλήθη, hashes πηγών, κανόνας βαρών και πληροφορίες διαχωρισμού. |

Τα CSV δεδομένων έχουν 27 στήλες: τις 26 του dataset1 και το split_group. Το split_group εξαιρείται από τα features.

Τα αντίστοιχα metadata έχουν έξι πεδία: record_id, study_id, coefficient_parameter, sample_weight, partition, rng_seed. Η δομή τους διαφέρει από τα 10 πεδία του βασικού dataset1_metadata.csv.

Σήμερα υπάρχουν 69 ομάδες και 17 μελέτες. Η μελέτη HARQ ON/OFF έχει μία μόνο ομάδα και βρίσκεται ολόκληρη στο validation. Τα πλήθη περιγράφουν την τρέχουσα έκδοση και μπορεί να αλλάξουν με νέα δεδομένα.

## Εκτέλεση προετοιμασίας

Από τη ρίζα του repository, σε Windows CMD:

```cmd
py -m venv .venv
.venv\Scripts\python.exe -m pip install -r Dataset\requirements.txt
.venv\Scripts\python.exe -X utf8 Dataset\prepare_weights.py
```

Αν υπάρχει ήδη κατάλληλο virtual environment, παραλείψτε τη δημιουργία του.
Σε Linux ή WSL:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r Dataset/requirements.txt
.venv/bin/python -X utf8 Dataset/prepare_weights.py
```

Εκτελείτε μόνο το prepare_weights.py: καλεί αυτόματα το resplit_for_coverage.py και μετά το export_metadata.py. Δεν χρειάζεται πρόσβαση στον simulator, στα αρχεία των config_path/results_path ή στο original_dataset.csv για την προετοιμασία. Οι διαδρομές προσομοιώσεων είναι πληροφορίες προέλευσης από το αρχικό περιβάλλον, όχι φορητές διαδρομές του repository.

Η εντολή ξαναγράφει τα CSV/JSON του Dataset/prepared και ενημερώνει split_group και partition στο Dataset/dataset1_metadata.csv. Δεν αλλάζει τις μετρήσεις του dataset1.csv, δεν εκπαιδεύει μοντέλο, δεν εκτελεί cross-validation και δεν υπολογίζει σφάλματα προβλέψεων. Τα train/validation εδώ είναι διαχωρισμένα δεδομένα, όχι αποτελέσματα εκπαίδευσης.

Κλείστε τα CSV στο Excel πριν την εκτέλεση. Αν υπάρξει σφάλμα, ενδέχεται να παραμείνουν ενδιάμεσα αρχεία· χρησιμοποιήστε τα outputs μόνο αφού ολοκληρωθεί επιτυχώς όλη η διαδικασία.

## Αντιστοίχιση γραμμών

Το csv_row μετρά και την κεφαλίδα: 2 σημαίνει πρώτη μέτρηση. Διατήρησε συγχρονισμένη τη σειρά των δύο βασικών CSV. Η αρίθμηση από μόνη της δεν εντοπίζει ανεξάρτητη αναδιάταξη των τιμών στο dataset1, επειδή εκεί δεν υπάρχει record_id.

Για τα prepared αρχεία, το metadata_manifest.json καταγράφει hashes ώστε ο μελλοντικός κώδικας εκπαίδευσης να μπορεί να ελέγξει την αντιστοίχιση πριν συνδέσει δεδομένα και metadata. Ο κώδικας εκπαίδευσης δεν αποτελεί μέρος αυτού του φακέλου. Μην επεξεργάζεσαι ανεξάρτητα τα παραγόμενα CSV· ενημέρωσε τις βασικές πηγές και αναδημιούργησε το prepared.

## Οδηγοί

Τα dataset1_parameters.docx και dataset1_metadata_parameters.docx εξηγούν τα βασικά πεδία. Οι δύο αντίστοιχοι οδηγοί στο prepared εξηγούν τα παραγόμενα πεδία. Τα έγγραφα αυτά δεν εκτελούνται από τον κώδικα.

## Δομή

```text
Dataset/
  dataset1.csv
  dataset1_metadata.csv
  prepare_weights.py
  resplit_for_coverage.py
  export_metadata.py
  requirements.txt
  README.md
  dataset1_parameters.docx
  dataset1_metadata_parameters.docx
  sources/
    impact_coefficients.xlsx
  prepared/
    dataset.csv, dataset_metadata.csv
    train.csv, train_metadata.csv
    validation.csv, validation_metadata.csv
    schema.json, metadata_manifest.json
    weight_mapping.csv, split_manifest.csv
    study_coverage.csv, previous_study_coverage.csv, summary.json
    dataset_parameters.docx, dataset_metadata_parameters.docx
```
