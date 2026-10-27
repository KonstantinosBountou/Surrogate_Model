"""Compare categorical TDD against categorical + structural TDD on training only."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import pandas as pd
from data_loading import load_prepared
from sklearn.model_selection import GroupKFold
from surrogate_model import prepare_features, make_model, metric_rows, TARGETS, NUMERIC


def structural(pattern, numerology):
    # Τα patterns επαναλαμβάνονται κυκλικά. Διατηρούμε και την αρχική φάση.
    slots = str(pattern).strip('|').split('|')
    if not slots or any(s not in ('UL', 'DL') for s in slots):
        raise ValueError(f'Unsupported TDD pattern: {pattern}')
    n = len(slots)
    result = {'tdd_slots': n, 'tdd_period_ms': n * 2.0 ** (-float(numerology)),
              'tdd_transitions': sum(slots[i] != slots[(i+1) % n] for i in range(n))}
    for direction in ('UL', 'DL'):
        count = slots.count(direction)
        result[f'tdd_{direction}_count'] = count
        result[f'tdd_{direction}_fraction'] = count / n
        result[f'tdd_{direction}_first_position'] = slots.index(direction) / n if count else -1
        # Μέγιστη κυκλική αναμονή σε slots μέχρι slot της ζητούμενης κατεύθυνσης.
        result[f'tdd_{direction}_max_wait_slots'] = max(next(k for k in range(n) if slots[(i+k) % n] == direction) for i in range(n)) if count else n
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--train', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError('Output folder must be new or empty')
    d = load_prepared(args.train)
    if 'partition' in d and not d.partition.eq('train').all():
        raise ValueError('Only training rows are allowed')
    y = d[TARGETS].to_numpy(float)
    assert np.isfinite(y).all() and d.record_id.is_unique
    # Οι υπολογισμοί χρησιμοποιούν μόνο τις εισόδους, χωρίς fit ή στόχους.
    extra = pd.DataFrame([structural(p, mu) for p, mu in zip(d.tdd_pattern, d.numerology)])
    x = prepare_features(d)
    enhanced = pd.concat([x, extra], axis=1)
    folds = list(GroupKFold(n_splits=5).split(x, groups=d.split_group))
    metrics, fold_metrics, predictions, assignments = [], [], [], []
    for variant, inputs in [('categorical', x), ('categorical_plus_structure', enhanced)]:
        oof = np.zeros_like(y)
        for fold, (tr, te) in enumerate(folds, 1):
            assert not set(d.iloc[tr].split_group) & set(d.iloc[te].split_group)
            model = make_model()
            if variant != 'categorical':
                # Ίδιο μοντέλο, ίδια imputation, μόνη αλλαγή οι πρόσθετες είσοδοι.
                transforms = model.named_steps['preprocess'].transformers
                name, transformer, _ = transforms[0]
                transforms[0] = (name, transformer, NUMERIC + list(extra.columns))
            model.fit(inputs.iloc[tr], y[tr])
            oof[te] = model.predict(inputs.iloc[te])
            rows = metric_rows(y[te], oof[te], variant)
            for j, row in enumerate(rows):
                row.update(fold=fold, normalized_mae=row['mae'] / max(np.std(y[tr, j]), 1e-12))
            fold_metrics.extend(rows)
            if variant == 'categorical':
                assignments.extend(dict(record_id=d.iloc[i].record_id, split_group=d.iloc[i].split_group, fold=fold) for i in te)
            print(f'{variant}: fold {fold}/5 complete', flush=True)
        metrics.extend(metric_rows(y, oof, variant))
        pred = d[['record_id', 'split_group']].copy()
        pred['variant'] = variant
        for j, target in enumerate(TARGETS):
            pred[target + '_actual'] = y[:, j]
            pred[target + '_predicted'] = oof[:, j]
        predictions.append(pred)
    args.output.mkdir(parents=True, exist_ok=True)
    pd.DataFrame(metrics).to_csv(args.output/'metrics.csv', index=False)
    pd.DataFrame(fold_metrics).to_csv(args.output/'fold_metrics.csv', index=False)
    pd.DataFrame(assignments).to_csv(args.output/'fold_assignments.csv', index=False)
    pd.concat(predictions).to_csv(args.output/'oof_predictions.csv', index=False)
    pd.concat([d[['record_id']], extra], axis=1).to_csv(args.output/'structural_features.csv', index=False)
    (args.output/'run.json').write_text(json.dumps(dict(training_sha256=hashlib.sha256(args.train.read_bytes()).hexdigest(), training_rows=len(d), features=list(extra.columns), validation_used=False, model='ExtraTrees 160 trees, min_samples_leaf=2, random_state=42', limitation='Exploratory paired CV; feature design informed by previously inspected validation. Not external test evidence.'), indent=2), encoding='utf8')
    print(pd.DataFrame(metrics).to_string(index=False))
    print('Mean normalized fold MAE:')
    print(pd.DataFrame(fold_metrics).groupby('evaluation').normalized_mae.mean().to_string())


if __name__ == '__main__':
    main()
