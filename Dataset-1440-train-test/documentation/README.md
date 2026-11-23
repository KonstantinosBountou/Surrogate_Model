# Οδηγός αρχείων του Dataset 1440 train test

Όλα τα paths είναι σχετικά με τον γενικό φάκελο `Dataset-1440-train-test/`. Ο οδηγός περιγράφει το δημοσιευμένο πακέτο, την προέλευση και τη χρήση κάθε αρχείου.

## Ροή δεδομένων

`configurations/*/results.json` → `dataset/dataset.csv` + `dataset/metadata.csv` → μέσοι όροι 5 runs και σταθερό split → εκπαίδευση τριών metrics και ξεχωριστού delay → `compare.py` + πραγματικά test runs → συγκεντρωτικά και αναλυτικά αποτελέσματα.

Το dataset περιέχει 1.440 runs / 288 configurations. Training: 1.130 runs / 226 means. Validation: 310 runs / 62 means. Το πρόσθετο benchmark έχει 120 runs / 24 configurations και δεν ανήκει στις 1.440 γραμμές. Δεν έχει αποθηκευτεί ξεχωριστός πίνακας σφάλματος πάνω στο ίδιο το training set.

## Κοινή δομή των προσομοιώσεων

| Path | Προέλευση και χρήση |
|---|---|
| `configurations/run_0001/` έως `run_1440/`, `config.json` | Οι πραγματικές ρυθμίσεις που δόθηκαν στον C++ simulator για κάθε dataset run. |
| Ίδιοι φάκελοι, `results.json` | Τα μετρημένα outputs ns-3. Συνδέονται με τη γραμμή dataset μέσω metadata και SHA-256. |
| `test-configurations/runs/caseXX_DDDm_r100Y/config.json` | Input της αντίστοιχης πραγματικής test προσομοίωσης. |
| Ίδιοι test φάκελοι, `results.json` | Αποτέλεσμα simulator για ένα από τα 120 test runs. Δεν είναι πρόβλεψη surrogate. |

Κάθε run έχει δύο JSON. Δεν περιλαμβάνονται εδώ simulator binaries, stdout/stderr logs ή ο πλήρης διορθωμένος ns-3 build. Τα αρχεία αυτά τεκμηριώνουν τις ολοκληρωμένες προσομοιώσεις· δεν αρκούν μόνα τους για εγκατάσταση του simulator.

## Αναλυτικός οδηγός των υπόλοιπων αρχείων

|---|---|

### Ριζικός φάκελος

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [.gitattributes](../.gitattributes) | Συσκευασία/φορητή τεκμηρίωση του repository | Διατηρεί τα bytes των πρωτογενών δεδομένων/JSON χωρίς μετατροπή line endings και χαρακτηρίζει τα joblib ως δυαδικά. |
| [.gitignore](../.gitignore) | Συσκευασία/φορητή τεκμηρίωση του repository | Κανόνες Git: επιτρέπει τα CSV αυτού του πακέτου, αποκλείει το virtual environment, caches και τοπικές επανεκτελέσεις. |
| [README.md](../README.md) | Συσκευασία/φορητή τεκμηρίωση του repository | Κεντρικές οδηγίες εγκατάστασης, εκπαίδευσης και σύγκρισης, διαχωρισμός δεδομένων και αξιολόγηση αποτελεσμάτων. |

### codes

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [compare.py](../codes/compare.py) | Συσκευασία/φορητή τεκμηρίωση του repository | Φορητός κώδικας πρόβλεψης τεσσάρων metrics. Φορτώνει επιλεγμένα μοντέλα, προβλέπει τα 24 configurations και συγκρίνει με μέσους όρους που επανυπολογίζει από τα 120 test runs. |
| [requirements.txt](../codes/requirements.txt) | Συσκευασία/φορητή τεκμηρίωση του repository | Ακριβείς εκδόσεις Python βιβλιοθηκών του ελεγμένου περιβάλλοντος αναπαραγωγής. Εγκατάσταση με pip install -r codes/requirements.txt. |
| [train_delay.py](../codes/train_delay.py) | Συσκευασία/φορητή τεκμηρίωση του repository | Φορητή προσαρμογή της εκπαίδευσης delay 1440. Διαβάζει το split του πρώτου script και επιλέγει ξεχωριστά μοντέλα HARQ-on/off με εσωτερικά holdouts μόνο του training. |
| [train_remaining.py](../codes/train_remaining.py) | Συσκευασία/φορητή τεκμηρίωση του repository | Φορητή προσαρμογή του κώδικα 1440. Συγκεντρώνει τα runs σε μέσους όρους, διατηρεί το split και εκπαιδεύει ExtraTrees/CatBoost/GP και σταθμισμένες παραλλαγές για throughput, jitter, packet loss. |
| [weight_policy.json](../codes/weight_policy.json) | Συσκευασία/φορητή τεκμηρίωση του repository | Ευρετικοί συντελεστές βαρών, ομάδες παραμέτρων και baseline. Χρησιμοποιείται μόνο στις σταθμισμένες παραλλαγές ExtraTrees/CatBoost, όχι στα τελικά GP. |

### dataset

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [dataset.csv](../dataset/dataset.csv) | Ενοποιημένη έκδοση dataset 1440 | 1.440 πραγματικά runs με εισόδους και τέσσερα outputs. Προέρχεται από την έκδοση 1440: 960 + 120 + 120 + 240 runs. |
| [metadata.csv](../dataset/metadata.csv) | Ενοποιημένη έκδοση dataset 1440 | Μία εγγραφή ανά γραμμή dataset στην ίδια σειρά: simulation ID, seed/run, partition, validation scope, σχετικά paths και SHA-256 αποτελεσμάτων. Τα paths προσαρμόστηκαν για το repository. |
| [schema.json](../dataset/schema.json) | Ενοποιημένη έκδοση dataset 1440 | Ορίζει numeric/categorical inputs και target metrics. Χρησιμοποιείται για ανάγνωση δεδομένων και προεπεξεργασία. |

### results/reference/comparison

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [benchmark24_comparison_all_metrics.csv](../results/reference/comparison/benchmark24_comparison_all_metrics.csv) | Εκτέλεση του φορητού compare.py με τα reference μοντέλα | Αναλυτική σύγκριση 24 configurations: πραγματικοί μέσοι όροι, προβλέψεις και απόλυτα σφάλματα και για τα τέσσερα metrics. |
| [benchmark24_metrics_all.csv](../results/reference/comparison/benchmark24_metrics_all.csv) | Εκτέλεση του φορητού compare.py με τα reference μοντέλα | Συγκεντρωτικά test MAE, RMSE, R² και αριθμός αρνητικών προβλέψεων για τα τέσσερα metrics. |

### results/reference/delay

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [benchmark24_comparison.csv](../results/reference/delay/benchmark24_comparison.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Αρχείο της προσπάθειας delay 1440: περιέχει τις νέες στήλες predicted_1440_delay_ms / absolute_error_1440_delay_ms μαζί με ιστορικές στήλες παλαιότερων μοντέλων. Οι υπόλοιπες προβλέψεις δεν είναι η τελική νέα εκπαίδευση των τριών metrics. |
| [benchmark24_metrics.csv](../results/reference/delay/benchmark24_metrics.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Σύγκριση μόνο του delay μεταξύ 1080, 1200, 1200_v2 και 1440 στο ίδιο benchmark. |
| [configuration_means.csv](../results/reference/delay/configuration_means.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Συγκεντρωτικές εγγραφές για όλα τα 288 configurations: inputs, μέσοι στόχοι και *_sem2 = δειγματική διακύμανση / 5. |
| [inner_metrics.csv](../results/reference/delay/inner_metrics.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | MAE/RMSE/R² υποψήφιων μοντέλων delay στα τρία εσωτερικά grouped holdouts του training, ανά κατάσταση HARQ. |
| [inner_split_manifest.csv](../results/reference/delay/inner_split_manifest.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Καταγραφή configurations που χρησιμοποιήθηκαν για fitting ή εσωτερική αξιολόγηση σε κάθε επαναλαμβανόμενο holdout του delay. |
| [lineage.csv](../results/reference/delay/lineage.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Αντιστοιχίζει κάθε configuration mean στα πέντε simulation IDs από τα οποία υπολογίστηκε. |

### results/reference/delay/models

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [delay_harq_False.joblib](../results/reference/delay/models/delay_harq_False.joblib) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Αποθηκευμένο εκπαιδευμένο μοντέλο και πληροφορίες μετασχηματισμών. HARQ-off CatBoost/raw. |
| [delay_harq_True.joblib](../results/reference/delay/models/delay_harq_True.joblib) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Αποθηκευμένο εκπαιδευμένο μοντέλο και πληροφορίες μετασχηματισμών. HARQ-on ExtraTrees/raw. |

### results/reference/delay

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [selection_before_outer.json](../results/reference/delay/selection_before_outer.json) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Οι νικητές επιλογής delay ανά HARQ πριν από την αξιολόγηση στο εξωτερικό validation/benchmark. |
| [split_manifest.csv](../results/reference/delay/split_manifest.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Διαχωρισμός configuration means σε train/validation, με IDs και validation scopes. Το delay αντίγραφο περιέχει επιπλέον τιμές από το αρχικό aggregation. |
| [train_means.csv](../results/reference/delay/train_means.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | 226 configuration means που χρησιμοποιούνται για fitting. Δεν περιέχει MAE/RMSE/R² πάνω στο training set. |
| [train_metadata.csv](../results/reference/delay/train_metadata.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Προέλευση και IDs των 1.130 training runs. Αρχειακές στήλες paths μπορεί να αφορούν την αρχική εγκατάσταση. |
| [validation_means.csv](../results/reference/delay/validation_means.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | 62 configuration means του validation. Δεν χρησιμοποιούνται για fitting των αποθηκευμένων τελικών μοντέλων. |
| [validation_metadata.csv](../results/reference/delay/validation_metadata.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Προέλευση και IDs των 310 validation runs. Για φορητά paths χρησιμοποιήστε το dataset/metadata.csv. |
| [validation_metrics.csv](../results/reference/delay/validation_metrics.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | MAE/RMSE/R² στο validation και ανά υποσύνολο. subset=all αντιστοιχεί στα 62 configurations. Στο delay αφορά το επιλεγμένο HARQ-routed μοντέλο· στο remaining_metrics όλους τους υποψηφίους των τριών targets. |
| [validation_predictions.csv](../results/reference/delay/validation_predictions.csv) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Πραγματικές και προβλεπόμενες τιμές validation. Στο delay αφορά το επιλεγμένο μοντέλο· στο remaining_metrics κάθε υποψήφιο και target. |
| [verification.json](../results/reference/delay/verification.json) | Αρχική εκπαίδευση delay της έκδοσης 1440 | Αρχειακή επιβεβαίωση ολοκλήρωσης της εκπαίδευσης delay, αριθμών train/validation και επιλεγμένων μοντέλων. Δεν είναι νέος ανεξάρτητος έλεγχος γενίκευσης. |

### results/reference/remaining_metrics

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [benchmark24_comparison_all_metrics.csv](../results/reference/remaining_metrics/benchmark24_comparison_all_metrics.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Αναλυτική σύγκριση 24 configurations: πραγματικοί μέσοι όροι, προβλέψεις και απόλυτα σφάλματα και για τα τέσσερα metrics. |
| [benchmark24_metrics_all.csv](../results/reference/remaining_metrics/benchmark24_metrics_all.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Συγκεντρωτικά test MAE, RMSE, R² και αριθμός αρνητικών προβλέψεων για τα τέσσερα metrics. |
| [configuration_means.csv](../results/reference/remaining_metrics/configuration_means.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Συγκεντρωτικές εγγραφές για όλα τα 288 configurations: inputs, μέσοι στόχοι και *_sem2 = δειγματική διακύμανση / 5. |
| [core_winners_by_rmse.csv](../results/reference/remaining_metrics/core_winners_by_rmse.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Νικητές validation RMSE μεταξύ των τριών βασικών αλγορίθμων, πριν συμπεριληφθούν οι δύο σταθμισμένες παραλλαγές. |
| [fit_times.csv](../results/reference/remaining_metrics/fit_times.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Καταγεγραμμένος χρόνος fitting ανά υποψήφιο και target κατά την αξιολόγηση. Εξαρτάται από hardware και περιβάλλον. |
| [lineage.csv](../results/reference/remaining_metrics/lineage.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Αντιστοιχίζει κάθε configuration mean στα πέντε simulation IDs από τα οποία υπολογίστηκε. |

### results/reference/remaining_metrics/models

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [GaussianProcess_jitter_ms.joblib](../results/reference/remaining_metrics/models/GaussianProcess_jitter_ms.joblib) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Αποθηκευμένο εκπαιδευμένο μοντέλο και πληροφορίες μετασχηματισμών. Επιλεγμένο GaussianProcess για το target που δηλώνει το όνομα. |
| [GaussianProcess_packet_loss_pct.joblib](../results/reference/remaining_metrics/models/GaussianProcess_packet_loss_pct.joblib) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Αποθηκευμένο εκπαιδευμένο μοντέλο και πληροφορίες μετασχηματισμών. Επιλεγμένο GaussianProcess για το target που δηλώνει το όνομα. |
| [GaussianProcess_throughput_mbps.joblib](../results/reference/remaining_metrics/models/GaussianProcess_throughput_mbps.joblib) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Αποθηκευμένο εκπαιδευμένο μοντέλο και πληροφορίες μετασχηματισμών. Επιλεγμένο GaussianProcess για το target που δηλώνει το όνομα. |

### results/reference/remaining_metrics

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [protocol.json](../results/reference/remaining_metrics/protocol.json) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Ρυθμίσεις αλγορίθμων, περιγραφή split, πολιτική βαρών και hashes αρχικών δεδομένων. Περιλαμβάνει ιστορικά paths της αρχικής εκτέλεσης. |
| [selected_models.csv](../results/reference/remaining_metrics/selected_models.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Επιλεγμένος αλγόριθμος ανά ένα από τα τρία targets και οι validation δείκτες του. |
| [selected_models.json](../results/reference/remaining_metrics/selected_models.json) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Αντιστοίχιση των τριών targets στα ονόματα των επιλεγμένων joblib. Διαβάζεται από το compare.py. |
| [split_audit.json](../results/reference/remaining_metrics/split_audit.json) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Έλεγχοι πληθών και διαχωρισμού, άγνωστων κατηγοριών και αριθμητικών ορίων στο validation. |
| [split_manifest.csv](../results/reference/remaining_metrics/split_manifest.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Διαχωρισμός configuration means σε train/validation, με IDs και validation scopes. Το delay αντίγραφο περιέχει επιπλέον τιμές από το αρχικό aggregation. |
| [status.json](../results/reference/remaining_metrics/status.json) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Κατάσταση ολοκλήρωσης, μέγεθος dataset, υποψήφια μοντέλα, έλεγχος επαναφόρτωσης και εκδόσεις βιβλιοθηκών. Αναφέρει 15 εκπαιδευμένα μοντέλα, αλλά δημοσιεύονται μόνο τα 3 επιλεγμένα αυτού του κλάδου. |
| [train_means.csv](../results/reference/remaining_metrics/train_means.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | 226 configuration means που χρησιμοποιούνται για fitting. Δεν περιέχει MAE/RMSE/R² πάνω στο training set. |
| [train_metadata.csv](../results/reference/remaining_metrics/train_metadata.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Προέλευση και IDs των 1.130 training runs. Αρχειακές στήλες paths μπορεί να αφορούν την αρχική εγκατάσταση. |
| [train_runs.csv](../results/reference/remaining_metrics/train_runs.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Οι 1.130 αρχικές γραμμές προσομοιώσεων που ανήκουν στο training, πριν από το averaging. |
| [validation_means.csv](../results/reference/remaining_metrics/validation_means.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | 62 configuration means του validation. Δεν χρησιμοποιούνται για fitting των αποθηκευμένων τελικών μοντέλων. |
| [validation_metadata.csv](../results/reference/remaining_metrics/validation_metadata.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Προέλευση και IDs των 310 validation runs. Για φορητά paths χρησιμοποιήστε το dataset/metadata.csv. |
| [validation_metrics.csv](../results/reference/remaining_metrics/validation_metrics.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | MAE/RMSE/R² στο validation και ανά υποσύνολο. subset=all αντιστοιχεί στα 62 configurations. Στο delay αφορά το επιλεγμένο HARQ-routed μοντέλο· στο remaining_metrics όλους τους υποψηφίους των τριών targets. |
| [validation_predictions.csv](../results/reference/remaining_metrics/validation_predictions.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Πραγματικές και προβλεπόμενες τιμές validation. Στο delay αφορά το επιλεγμένο μοντέλο· στο remaining_metrics κάθε υποψήφιο και target. |
| [validation_runs.csv](../results/reference/remaining_metrics/validation_runs.csv) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Οι 310 αρχικές γραμμές προσομοιώσεων που ανήκουν στο validation, πριν από το averaging. |
| [warnings.json](../results/reference/remaining_metrics/warnings.json) | Μεταγενέστερη εκπαίδευση των άλλων τριών metrics στην 1440 | Προειδοποιήσεις βιβλιοθηκών που καταγράφηκαν στο fitting, π.χ. όρια υπερπαραμέτρων GP. Δεν είναι αποτελέσματα προσομοιώσεων. |

### test-configurations

| Αρχείο | Προέλευση | Περιγραφή και χρήση |
|---|---|---|
| [configurations_24.csv](../test-configurations/configurations_24.csv) | Κοινό benchmark 24 configurations / 120 runs | 24 input configurations του διαγνωστικού benchmark, αρχικά επιλεγμένα για την έκδοση 960 και επαναχρησιμοποιημένα στις συγκρίσεις. |
| [ns3_runs_120.csv](../test-configurations/ns3_runs_120.csv) | Κοινό benchmark 24 configurations / 120 runs | Πραγματικές μετρήσεις τεσσάρων metrics για 24 configurations × 5 runs, seed 42 και runs 1001–1005. Δεν είναι προβλέψεις μοντέλου. |

## Σημαντικές διακρίσεις

| Θέμα | Διευκρίνιση |
|---|---|

| Αρχικά και νέα αποτελέσματα | Τα `results/reference/` είναι δημοσιευμένα αποτελέσματα της ελεγμένης εκτέλεσης. Τα νέα τοπικά αποτελέσματα γράφονται στο `results/reproduced/`, το οποίο αποκλείεται από το Git. |
| Περιεχόμενο μέσων όρων | Στα αρχειακά `delay/train_means.csv`, `validation_means.csv` και `configuration_means.csv` υπάρχουν και τα τέσσερα targets. Στα αρχειακά αντίστοιχα `remaining_metrics/` υπάρχουν τα τρία targets. Ο φορητός κώδικας πλέον συγκεντρώνει και τα τέσσερα για να τροφοδοτεί και την εκπαίδευση delay. |
| Συγκεντρωτικά test αποτελέσματα | Τα δύο combined benchmark CSV στο `remaining_metrics/` είναι ιστορικά. Για την ενιαία σύγκριση προτιμήστε τα αντίστοιχα του `comparison/`, τα οποία παράγει το φορητό script. |
| Όρια αξιολόγησης | Το κοινό benchmark καθοδήγησε βελτιώσεις και είναι διαγνωστικό. Απαιτείται νέο ανεξάρτητο test. Οι αρνητικές προβλέψεις packet loss αναφέρονται χωρίς clipping. |
| Διαδρομές αρχείων | Τα παλαιά paths στα αρχειακά reports δεν απαιτούνται για τη φορητή εκτέλεση. Το `dataset/metadata.csv` περιέχει τα σχετικά paths του πακέτου. |

## Αρχείο τεκμηρίωσης

| Θέμα | Περιγραφή |
|---|---|

| Τεκμηρίωση | `documentation/README.md`: ο παρών οδηγός. |

Για τις εντολές εγκατάστασης, training και test, δείτε το [κεντρικό README](../README.md).
