from __future__ import annotations

import pathlib
import sys
import unittest

import numpy as np
import pandas as pd

ANALYSIS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ANALYSIS))

import blind  # noqa: E402
import confirm  # noqa: E402


def synthetic_runs(
    ratio: float,
    noise: float = 0.0,
    pair_noise: float = 0.0,
    trials: int = 8,
    cells: int = 1,
    capped_trials: tuple[int, ...] = (),
) -> pd.DataFrame:
    """Paired subject/baseline trials with a known throughput ratio.

    ``noise`` is shared within a trial (the machine state both structures see;
    pairing cancels it), ``pair_noise`` is drawn per structure (what pairing
    cannot cancel, and what widens the ratio interval).
    """
    rng = np.random.default_rng(7)
    rows = []
    for cell in range(cells):
        for trial in range(trials):
            base = 100.0 * (1 + cell)
            wobble = float(np.exp(rng.normal(0.0, noise)))
            status = "memory_cap" if trial in capped_trials else "ok"
            for structure, value in (
                ("persistent", base * wobble * float(np.exp(rng.normal(0.0, pair_noise)))),
                ("copy-on-push",
                 base * ratio * wobble * float(np.exp(rng.normal(0.0, pair_noise)))),
            ):
                rows.append(
                    {
                        "workload": f"W{cell + 1}",
                        "n": 1000,
                        "axis": "none",
                        "variant": 0.0,
                        "structure": structure,
                        "trial": trial,
                        "seed": 100 + trial,
                        "status": status if structure == "copy-on-push" else "ok",
                        "complete": not (structure == "copy-on-push" and status != "ok"),
                        "update_ns_per_op": value,
                        "checksum": 42,
                    }
                )
    return pd.DataFrame(rows)


class ClassificationTests(unittest.TestCase):
    def test_four_states(self) -> None:
        margin = confirm.LOG_DELTA
        self.assertEqual(confirm.classify(margin * 1.01, margin * 2), "meaningfully faster")
        self.assertEqual(confirm.classify(-margin * 2, -margin * 1.01), "meaningfully slower")
        self.assertEqual(
            confirm.classify(-margin * 0.5, margin * 0.5), "practically equivalent"
        )
        self.assertEqual(confirm.classify(-margin * 0.5, margin * 2), "inconclusive")
        self.assertEqual(confirm.classify(float("nan"), 0.0), "inconclusive")

    def test_clear_effect_is_meaningfully_faster(self) -> None:
        table = confirm.paired_cell_ratios(
            synthetic_runs(ratio=1.5, noise=0.01, trials=20), "persistent", "update_ns_per_op"
        )
        self.assertEqual(len(table), 1)
        self.assertEqual(table["classification"].iloc[0], "meaningfully faster")
        self.assertAlmostEqual(table["ratio"].iloc[0], 1.5, delta=0.05)

    def test_wide_interval_is_underpowered(self) -> None:
        table = confirm.paired_cell_ratios(
            synthetic_runs(ratio=1.0, pair_noise=1.5, trials=6),
            "persistent",
            "update_ns_per_op",
        )
        self.assertEqual(table["classification"].iloc[0], "inconclusive")
        self.assertTrue(bool(table["underpowered"].iloc[0]))


class CensoringTests(unittest.TestCase):
    def test_capped_trials_never_enter_ratios(self) -> None:
        capped = synthetic_runs(ratio=1.5, trials=10, capped_trials=(0, 1, 2))
        table = confirm.paired_cell_ratios(capped, "persistent", "update_ns_per_op")
        self.assertEqual(int(table["pairs"].iloc[0]), 7)

    def test_too_few_pairs_reports_no_classification(self) -> None:
        capped = synthetic_runs(ratio=1.5, trials=5, capped_trials=(0, 1))
        table = confirm.paired_cell_ratios(capped, "persistent", "update_ns_per_op")
        self.assertEqual(table["classification"].iloc[0], "")


class PrimaryFamilyTests(unittest.TestCase):
    def test_family_filters_to_registered_cells_and_applies_holm(self) -> None:
        runs = synthetic_runs(ratio=1.4, noise=0.01, trials=24, cells=3)
        primary = pd.DataFrame(
            [{"workload": "W1", "n": 1000, "variant": 0.0}]
        )
        table = confirm.primary_family(runs, primary)
        self.assertEqual(len(table), 1)
        self.assertGreaterEqual(table["p_holm"].iloc[0], table["p"].iloc[0])

    def test_underpowered_cell_cannot_be_equivalent(self) -> None:
        runs = synthetic_runs(ratio=1.0, noise=0.001, trials=10)
        primary = pd.DataFrame([{"workload": "W1", "n": 1000, "variant": 0.0}])
        table = confirm.primary_family(runs, primary)
        # Ten pairs sit below the registered minimum of twenty trials, so a
        # tight interval still cannot support practical equivalence.
        self.assertTrue(bool(table["underpowered"].iloc[0]))
        self.assertNotEqual(table["classification"].iloc[0], "practically equivalent")


class ChecksumTests(unittest.TestCase):
    def test_disagreement_is_reported(self) -> None:
        runs = synthetic_runs(ratio=1.2, trials=4)
        runs.loc[
            (runs["structure"] == "copy-on-push") & (runs["trial"] == 2), "checksum"
        ] = 43
        failures = confirm.checksum_agreement(runs)
        self.assertEqual(len(failures), 1)

    def test_agreement_is_silent(self) -> None:
        self.assertTrue(confirm.checksum_agreement(synthetic_runs(ratio=1.2)).empty)


class H1Tests(unittest.TestCase):
    def h1_frames(self, offset: int = 0) -> tuple[pd.DataFrame, pd.DataFrame]:
        runs = pd.DataFrame(
            [
                {
                    "workload": "W1",
                    "n": 7,
                    "axis": "none",
                    "variant": 0.0,
                    "seed": 11,
                    "structure": "persistent",
                    "complete": True,
                    "nodes": 130 + offset,
                    "build_nodes": 13,
                },
                {
                    "workload": "W1",
                    "n": 7,
                    "axis": "none",
                    "variant": 0.0,
                    "seed": 11,
                    "structure": "copy-on-push",
                    "complete": True,
                    "nodes": 13 + 117 + 2 * 9,
                    "build_nodes": 13,
                },
            ]
        )
        structural = pd.DataFrame(
            [
                {
                    "workload": "W1",
                    "n": 7,
                    "axis": "none",
                    "variant": 0.0,
                    "seed": 11,
                    "sum_update_visits": 117,
                    "sum_pushes": 9,
                    "sum_intersecting": 200,
                    "nonzero_updates": 9,
                }
            ]
        )
        return runs, structural

    def test_exact_identities_pass(self) -> None:
        table = confirm.h1_identities(*self.h1_frames())
        self.assertTrue(bool(table["exact"].all()))

    def test_single_record_mismatch_fails(self) -> None:
        table = confirm.h1_identities(*self.h1_frames(offset=1))
        subject = table[table["structure"] == "persistent"]
        self.assertEqual(int(subject["mismatches"].iloc[0]), 1)
        self.assertFalse(bool(subject["exact"].iloc[0]))


class H4H5Tests(unittest.TestCase):
    def test_h4_agreement_share_and_disagreements(self) -> None:
        a = confirm.paired_cell_ratios(
            synthetic_runs(ratio=1.5, noise=0.01, trials=20, cells=2),
            "persistent",
            "update_ns_per_op",
        )
        b = a.copy()
        b.loc[0, "classification"] = "practically equivalent"
        result = confirm.h4_agreement(a, b)
        self.assertEqual(result["cells"], 2)
        self.assertAlmostEqual(result["agreement"], 0.5)
        self.assertEqual(len(result["disagreements"]), 1)

    def test_h5_region_counts_only_eligible_rows(self) -> None:
        actual = np.array([10.0, 10.0, 10.0, -1.0])
        predicted = np.array([10.0, 16.0, 14.0, 5.0])
        result = confirm.h5_region(actual, predicted)
        self.assertEqual(result["eligible"], 3)
        self.assertEqual(result["inside"], 2)


class PrecisionTests(unittest.TestCase):
    def test_more_spread_needs_more_trials(self) -> None:
        self.assertLess(confirm.required_trials(0.02), confirm.required_trials(0.3))

    def test_precision_table_reports_per_cell_requirements(self) -> None:
        table = confirm.precision(synthetic_runs(ratio=1.2, noise=0.05, trials=11))
        self.assertEqual(len(table), 1)
        self.assertGreaterEqual(int(table["required_trials"].iloc[0]), 4)


class HierarchicalTests(unittest.TestCase):
    def test_mixed_model_recovers_cell_effect(self) -> None:
        runs = synthetic_runs(ratio=1.5, noise=0.02, trials=16, cells=3)
        table = confirm.hierarchical(runs)
        self.assertEqual(len(table), 3)
        for effect in table["effect"]:
            self.assertAlmostEqual(effect, np.log(1.5), delta=0.05)


class BlindTests(unittest.TestCase):
    def test_mapping_is_deterministic_and_key_dependent(self) -> None:
        first = blind.label_map(b"k" * 32)
        self.assertEqual(first, blind.label_map(b"k" * 32))
        self.assertNotEqual(first, blind.label_map(b"j" * 32))
        self.assertEqual(sorted(first.values()), [f"S{i:02d}" for i in range(1, 10)])

    def test_blind_frame_replaces_structures(self) -> None:
        runs = synthetic_runs(ratio=1.2, trials=4)
        blinded = blind.blind_frame(runs, b"k" * 32)
        self.assertFalse(set(blinded["structure"]) & set(blind.STRUCTURES))
        self.assertEqual(
            blinded["structure"].nunique(), runs["structure"].nunique()
        )


if __name__ == "__main__":
    unittest.main()
