from __future__ import annotations

import contextlib
import hashlib
import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np
import pandas as pd

ANALYSIS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ANALYSIS))

import blind  # noqa: E402
import confirm  # noqa: E402
import data  # noqa: E402


def synthetic_runs(
    ratio: float,
    noise: float = 0.0,
    pair_noise: float = 0.0,
    trials: int = 8,
    cells: int = 1,
    capped_trials: tuple[int, ...] = (),
) -> pd.DataFrame:
    """Paired trials with known effects, censoring and balanced order."""
    rng = np.random.default_rng(7)
    rows = []
    for cell in range(cells):
        for trial in range(trials):
            base = 100.0 * (1 + cell)
            wobble = float(np.exp(rng.normal(0.0, noise)))
            status = "memory_cap" if trial in capped_trials else "ok"
            values = (
                ("persistent", base * wobble * float(np.exp(rng.normal(0.0, pair_noise)))),
                (
                    "copy-on-push",
                    base * ratio * wobble * float(np.exp(rng.normal(0.0, pair_noise))),
                ),
            )
            for offset, (structure, value) in enumerate(values):
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
                        "exec_order": (trial + offset) % 2,
                        "checksum": 42,
                    }
                )
    return pd.DataFrame(rows)


def timing_csv(runs: pd.DataFrame, path: pathlib.Path) -> None:
    """Write synthetic runs in the raw timing CSV schema that load_runs reads."""
    path.parent.mkdir(parents=True, exist_ok=True)
    runs.assign(
        update_ns=lambda frame: frame["update_ns_per_op"] * 10,
        query_ns=1000.0,
        updates=10,
        queries=10,
        batch_ns=2000.0,
        bytes=1024,
        alloc_peak_bytes=0,
        nodes=20,
        build_nodes=10,
        build_ns=100,
    ).drop(columns=["update_ns_per_op", "complete"]).to_csv(path, index=False)


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
            synthetic_runs(ratio=1.5, noise=0.01, trials=20),
            "persistent",
            "update_ns_per_op",
        )
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

    def test_all_zero_wilcoxon_is_exactly_one(self) -> None:
        statistic, pvalue = confirm.registered_wilcoxon(np.zeros(20))
        self.assertEqual((statistic, pvalue), (0.0, 1.0))

    def test_non_finite_paired_values_are_rejected(self) -> None:
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "non-finite"):
            confirm.registered_wilcoxon(np.array([0.1, np.nan]))
        runs = synthetic_runs(ratio=1.2)
        runs.loc[0, "update_ns_per_op"] = np.inf
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "non-finite"):
            confirm.paired_cell_ratios(runs, "persistent", "update_ns_per_op")


class NonFiniteResponseTests(unittest.TestCase):
    def test_nan_metric_in_complete_trial_fails_closed(self) -> None:
        runs = synthetic_runs(ratio=1.2, trials=20)
        runs.loc[0, "update_ns_per_op"] = np.nan
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "non-finite"):
            confirm.paired_cell_ratios(runs, "persistent", "update_ns_per_op")
        few = synthetic_runs(ratio=1.2, trials=3)
        few.loc[0, "update_ns_per_op"] = np.nan
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "non-finite"):
            confirm.paired_cell_ratios(few, "persistent", "update_ns_per_op")

    def test_zero_operation_count_is_not_silently_dropped(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            raw = pathlib.Path(directory)
            timing_csv(synthetic_runs(ratio=1.2, trials=20), raw / "runs_timing-W1.csv")
            frame = pd.read_csv(raw / "runs_timing-W1.csv")
            frame.loc[0, "updates"] = 0
            frame.to_csv(raw / "runs_timing-W1.csv", index=False)
            runs = data.load_runs(raw, "timing")
            self.assertEqual(runs.attrs["incomplete_rows"], 0)
            with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "non-finite"):
                confirm.paired_cell_ratios(runs, "persistent", "update_ns_per_op")


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
    @staticmethod
    def registry(trials: int) -> pd.DataFrame:
        return pd.DataFrame(
            [
                {
                    "workload": f"W{cell + 1}",
                    "n": 1000,
                    "axis": "none",
                    "variant": 0.0,
                    "trials": trials,
                }
                for cell in range(confirm.H2_EXPECTED_CELLS)
            ]
        )

    def test_family_filters_to_registered_cells_and_applies_holm(self) -> None:
        runs = synthetic_runs(ratio=1.4, noise=0.01, trials=24, cells=7)
        table = confirm.primary_family(runs, self.registry(24))
        self.assertEqual(len(table), confirm.H2_EXPECTED_CELLS)
        self.assertTrue((table["p_holm"] >= table["p"]).all())

    def test_underpowered_cell_cannot_be_equivalent(self) -> None:
        runs = synthetic_runs(
            ratio=1.0, noise=0.001, trials=10, cells=confirm.H2_EXPECTED_CELLS
        )
        table = confirm.primary_family(runs, self.registry(20))
        self.assertTrue(bool(table["underpowered"].all()))
        self.assertFalse((table["classification"] == "practically equivalent").any())

    def test_insufficient_pairs_cell_is_inconclusive(self) -> None:
        runs = synthetic_runs(
            ratio=1.5, noise=0.01, trials=20, cells=confirm.H2_EXPECTED_CELLS, capped_trials=(0,)
        )
        table = confirm.primary_family(runs, self.registry(20))
        self.assertTrue((table["pairs"] == 19).all())
        self.assertTrue((table["p_status"] == "insufficient_pairs").all())
        self.assertTrue((table["p"] == 1.0).all())
        self.assertTrue((table["classification"] == "inconclusive").all())
        self.assertTrue(bool(table["underpowered"].all()))
        self.assertTrue((table["ci_lo"] > confirm.LOG_DELTA).all())

    def test_holm_arithmetic(self) -> None:
        adjusted = data.holm([0.01, 0.04, 0.03, 0.005])
        np.testing.assert_allclose(adjusted, [0.03, 0.06, 0.06, 0.02])

    def test_primary_registry_requires_exactly_six_unique_cells(self) -> None:
        runs = synthetic_runs(
            ratio=1.2, trials=20, cells=confirm.H2_EXPECTED_CELLS
        )
        duplicate = pd.concat(
            [self.registry(20), self.registry(20).iloc[[0]]], ignore_index=True
        )
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "exactly 6"):
            confirm.primary_family(runs, duplicate)


class ChecksumTests(unittest.TestCase):
    def test_disagreement_is_reported(self) -> None:
        runs = synthetic_runs(ratio=1.2, trials=4)
        runs.loc[
            (runs["structure"] == "copy-on-push") & (runs["trial"] == 2), "checksum"
        ] = 43
        self.assertEqual(len(confirm.checksum_agreement(runs)), 1)

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
                {
                    "workload": "W1",
                    "n": 7,
                    "axis": "none",
                    "variant": 0.0,
                    "seed": 11,
                    "structure": "point-only",
                    "complete": True,
                    "nodes": 13 + 200,
                    "build_nodes": 13,
                },
                {
                    "workload": "W1",
                    "n": 7,
                    "axis": "none",
                    "variant": 0.0,
                    "seed": 11,
                    "structure": "full-copy",
                    "complete": True,
                    "nodes": 13 + 13 * 9,
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

    def test_exact_and_failing_identities(self) -> None:
        self.assertTrue(bool(confirm.h1_identities(*self.h1_frames())["exact"].all()))
        table = confirm.h1_identities(*self.h1_frames(offset=1))
        subject = table[table["structure"] == "persistent"]
        self.assertEqual(int(subject["mismatches"].iloc[0]), 1)
        self.assertFalse(bool(subject["exact"].iloc[0]))


class H3Tests(unittest.TestCase):
    def cells(self, ape: float = 10.0) -> pd.DataFrame:
        rows = []
        for structure in confirm.H3_STRUCTURES:
            for op in ("update", "query"):
                for cell in range(3):
                    actual = 100.0
                    predicted = actual * (1.0 + ape / 100.0)
                    rows.append(
                        {
                            "workload": f"W{cell + 1}",
                            "n": 1000,
                            "axis": "none",
                            "variant": 0.0,
                            "structure": structure,
                            "op": op,
                            "trials": 20 + cell,
                            "actual_median": actual,
                            "predicted_median": predicted,
                            "actual_mean": actual,
                            "predicted_mean": predicted,
                            "ape_median": ape,
                            "ape_mean": ape,
                            "expected_cell_count": 3,
                            "expected_inventory_sha256": "a" * 64,
                            "model_artifact_sha256": "b" * 64,
                        }
                    )
        return pd.DataFrame(rows)

    def test_equal_cell_weight_ignores_different_trial_counts(self) -> None:
        table = confirm.h3_decisions(self.cells())
        self.assertEqual(len(table), len(confirm.H3_STRUCTURES) * 2)
        self.assertTrue(bool(table["supported"].all()))
        self.assertTrue((table["median_ape"] == 10.0).all())

    def test_failing_and_missing_h3_data_are_explicit(self) -> None:
        self.assertFalse(bool(confirm.h3_decisions(self.cells(40.0))["supported"].any()))
        missing = self.cells()
        missing = missing[
            ~((missing["structure"] == "persistent") & (missing["op"] == "query"))
        ]
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "no holdout predictions"):
            confirm.h3_decisions(missing)
        short = self.cells()
        short = short.drop(short[(short["structure"] == "persistent")].index[:1])
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "requires 3"):
            confirm.h3_decisions(short)

    def test_per_structure_inventory_excludes_registered_caps(self) -> None:
        cells = self.cells()
        capped = cells["structure"] == "point-only"
        cells.loc[capped, "expected_cell_count"] = 2
        cells = cells.drop(cells[capped & (cells["workload"] == "W1")].index)
        table = confirm.h3_decisions(cells)
        point_only = table[table["structure"] == "point-only"]
        self.assertEqual(point_only["cells"].tolist(), [2, 2])
        self.assertTrue(bool(table["supported"].all()))
        mixed = self.cells()
        mixed.loc[mixed.index[0], "expected_cell_count"] = 2
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "disagree"):
            confirm.h3_decisions(mixed)

    def test_prediction_sidecar_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "cells.csv"
            self.cells().to_csv(path, index=False)
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            path.with_suffix(".csv.sha256").write_text(f"{digest}  {path.name}\n")
            self.assertEqual(confirm.verify_file_sidecar(path), digest)
            path.write_text(path.read_text() + "\n")
            with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "does not match"):
                confirm.verify_file_sidecar(path)


class H4H5Tests(unittest.TestCase):
    def h4_table(self) -> pd.DataFrame:
        return confirm.paired_cell_ratios(
            synthetic_runs(ratio=1.5, noise=0.01, trials=20, cells=12),
            "persistent",
            "update_ns_per_op",
        )

    def test_h4_requires_all_twelve_cells(self) -> None:
        a = self.h4_table()
        b = a.copy()
        b.loc[0, "classification"] = "practically equivalent"
        cells = a[confirm.CELL_KEYS].assign(trials=20)
        result = confirm.h4_agreement(a, b, cells)
        self.assertEqual(result["cells"], 12)
        self.assertAlmostEqual(result["agreement"], 11 / 12)
        self.assertEqual(len(result["disagreements"]), 1)
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "exactly one"):
            confirm.h4_agreement(a.iloc[:-1], a, cells)
        incomplete = a.copy()
        incomplete.loc[0, "pairs"] = 19
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "every registered paired"):
            confirm.h4_agreement(incomplete, a, cells)

    def test_versioned_h4_and_h5_registries_have_exact_membership(self) -> None:
        bench = ANALYSIS.parent
        h4 = confirm.normalize_registered_cells(
            pd.read_csv(bench / "h4_cells.csv"), confirm.H4_EXPECTED_CELLS
        )
        self.assertEqual(len(h4), 12)
        draws = pd.read_csv(bench / "h5_trace_draws.csv")
        self.assertEqual(draws["draw_id"].tolist(), [f"WT{i:02d}" for i in range(1, 13)])
        self.assertEqual(draws["seed"].tolist(), list(range(20270214, 20270226)))
        self.assertEqual(draws["trials"].tolist(), [20] * 12)

    def test_h5_rejects_invalid_and_missing_cells(self) -> None:
        result = confirm.h5_region(
            np.array([10.0, 10.0, 10.0]), np.array([10.0, 16.0, 14.0])
        )
        self.assertEqual((result["eligible"], result["inside"]), (3, 2))
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "non-positive"):
            confirm.h5_region(np.array([10.0, -1.0]), np.array([10.0, 5.0]))

        draws = pd.DataFrame(
            {
                "draw_id": [f"WT{i:02d}" for i in range(1, 13)],
                "seed": list(range(20270214, 20270226)),
                "n": 100000,
                "operations": 200000,
                "update_share": 0.5,
                "interval_share": 0.01,
                "trials": 20,
            }
        )
        cells = pd.DataFrame(
            [
                {
                    **draw.to_dict(),
                    "op": op,
                    "actual": 10.0,
                    "predicted": 10.0,
                    "source_model": "copy-on-push",
                    "target_structure": "external",
                    "model_artifact_sha256": "a" * 64,
                    "transfer_responses_sha256": "b" * 64,
                }
                for _, draw in draws.iterrows()
                for op in ("update", "query")
            ]
        )
        self.assertTrue(bool(confirm.h5_decision(cells, draws)["supported"].iloc[0]))
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "missing"):
            confirm.h5_decision(cells.iloc[:-1], draws)
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "extra"):
            extra = cells.iloc[[0]].assign(draw_id="WT99")
            confirm.h5_decision(pd.concat([cells, extra]), draws)
        changed = cells.copy()
        changed.loc[0, "seed"] = 99
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "seed"):
            confirm.h5_decision(changed, draws)
        changed = cells.copy()
        changed.loc[0, "trials"] = 19
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "trials"):
            confirm.h5_decision(changed, draws)


class PrecisionTests(unittest.TestCase):
    def test_more_spread_needs_more_trials(self) -> None:
        self.assertLess(confirm.required_trials(0.02), confirm.required_trials(0.3))

    def test_precision_table_reports_requirements(self) -> None:
        table = confirm.precision(synthetic_runs(ratio=1.2, noise=0.05, trials=11))
        self.assertEqual(len(table), 1)
        self.assertGreaterEqual(int(table["required_trials"].iloc[0]), 4)


class HierarchicalTests(unittest.TestCase):
    def test_mixed_model_recovers_effect_and_records_diagnostics(self) -> None:
        runs = synthetic_runs(ratio=1.5, noise=0.02, pair_noise=0.03, trials=16, cells=3)
        table = confirm.hierarchical(runs)
        self.assertEqual(len(table), 3)
        self.assertTrue(bool(table["converged"].all()))
        self.assertIn(table["optimizer"].iloc[0], confirm.MIXEDLM_OPTIMIZERS)
        self.assertTrue(table.attrs["fit_diagnostics"])
        for effect in table["effect"]:
            self.assertAlmostEqual(effect, np.log(1.5), delta=0.05)

    def test_singular_random_effect_fails_closed_with_diagnostics(self) -> None:
        runs = synthetic_runs(ratio=1.5, trials=8, cells=3)
        with self.assertRaises(confirm.RegisteredAnalysisError) as caught:
            confirm.hierarchical(runs)
        self.assertEqual(len(caught.exception.diagnostics), len(confirm.MIXEDLM_OPTIMIZERS))
        self.assertTrue(all("optimizer" in entry for entry in caught.exception.diagnostics))

    def test_non_convergence_fails_without_ols_fallback(self) -> None:
        runs = synthetic_runs(ratio=1.5, pair_noise=0.03, trials=16, cells=3)
        with mock.patch("statsmodels.formula.api.mixedlm") as mixedlm:
            mixedlm.return_value.fit.side_effect = RuntimeError("did not converge")
            with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "no fallback") as caught:
                confirm.hierarchical(runs)
        self.assertEqual(len(caught.exception.diagnostics), len(confirm.MIXEDLM_OPTIMIZERS))


class SensitivityTests(unittest.TestCase):
    def test_registered_sensitivity_outputs(self) -> None:
        runs = synthetic_runs(ratio=1.3, pair_noise=0.03, trials=20, cells=3)
        self.assertEqual(len(confirm.mean_median_sensitivity(runs)), 3)
        order = confirm.execution_order_regression(runs)
        self.assertEqual(
            order["order_variable"].iloc[0], "subject exec_order - baseline exec_order"
        )
        self.assertEqual(len(confirm.leave_one_trial_out(runs)), 60)

        registry = pd.read_csv(ANALYSIS.parent / "sensitivity_cells.csv")
        campaigns = []
        for ratio in (1.3, 1.2):
            frames = []
            for row in registry.itertuples(index=False):
                frame = synthetic_runs(ratio=ratio, pair_noise=0.02, trials=20)
                frame["workload"] = row.workload
                frame["n"] = row.n
                frame["axis"] = row.axis
                frame["variant"] = row.variant
                frames.append(frame)
            campaigns.append(pd.concat(frames, ignore_index=True))
        compiler = confirm.campaign_sensitivity(
            campaigns[0], campaigns[1], "compiler", registry
        )
        self.assertEqual(len(compiler), confirm.SENSITIVITY_EXPECTED_CELLS)
        self.assertTrue((compiler["dimension"] == "compiler").all())
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "missing or adds"):
            confirm.campaign_sensitivity(
                campaigns[0],
                campaigns[1][campaigns[1]["workload"] != "W5"],
                "allocator",
                registry,
            )

    def test_restrict_primary_effects_uses_the_full_cell_key(self) -> None:
        effects = pd.concat(
            [
                confirm.paired_effects(
                    synthetic_runs(
                        ratio=1.3, trials=6, cells=confirm.H2_EXPECTED_CELLS
                    ).assign(axis=axis),
                    "persistent",
                    "copy-on-push",
                    "update_ns_per_op",
                )
                for axis in ("none", "width")
            ],
            ignore_index=True,
        )
        registry = PrimaryFamilyTests.registry(20)
        selected = confirm.restrict_primary_effects(effects, registry, "order")
        self.assertEqual(set(selected["axis"]), {"none"})
        self.assertEqual(len(selected), 6 * confirm.H2_EXPECTED_CELLS)

    def test_constant_execution_order_is_rejected(self) -> None:
        runs = synthetic_runs(ratio=1.3, trials=20, cells=2)
        runs["exec_order"] = 0
        with self.assertRaisesRegex(confirm.RegisteredAnalysisError, "constant"):
            confirm.execution_order_regression(runs)

    def test_incomplete_sensitivity_cells_are_reported(self) -> None:
        runs = synthetic_runs(
            ratio=1.2, trials=3, cells=confirm.H2_EXPECTED_CELLS
        )
        registry = PrimaryFamilyTests.registry(20)
        mean_median = confirm.mean_median_sensitivity(
            runs, primary_cells=registry
        )
        leave_one_out = confirm.leave_one_trial_out(
            runs, primary_cells=registry
        )
        self.assertEqual(len(mean_median), confirm.H2_EXPECTED_CELLS)
        self.assertEqual(len(leave_one_out), confirm.H2_EXPECTED_CELLS)
        self.assertTrue(
            (mean_median["sensitivity_status"] == "insufficient_pairs").all()
        )
        self.assertTrue(
            (leave_one_out["sensitivity_status"] == "insufficient_pairs").all()
        )


class UnavailableRecordTests(unittest.TestCase):
    def test_stage_failure_writes_unavailable_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            campaign = pathlib.Path(directory)
            raw_file = campaign / "raw" / "runs_timing-W1.csv"
            timing_csv(synthetic_runs(ratio=1.2, trials=20), raw_file)
            registry = campaign / "primary.csv"
            PrimaryFamilyTests.registry(20).to_csv(registry, index=False)
            argv = ["--campaign", str(campaign), "--stage", "primary"]
            argv += ["--primary-cells", str(registry)]
            with self.assertRaisesRegex(SystemExit, "primary is unavailable"):
                with contextlib.redirect_stdout(io.StringIO()):
                    confirm.main(argv)
            output = campaign / "analysis" / "primary_update.csv"
            record = confirm.unavailable_record_path(output)
            self.assertFalse(output.exists())
            body = json.loads(record.read_text())
            self.assertEqual((body["stage"], body["status"]), ("primary", "unavailable"))
            self.assertIn("missing registered primary cells", body["reason"])
            self.assertRegex(body["recorded_utc"], r"Z$")

            timing_csv(
                synthetic_runs(ratio=1.2, trials=20, cells=confirm.H2_EXPECTED_CELLS), raw_file
            )
            with contextlib.redirect_stdout(io.StringIO()):
                confirm.main(argv)
            self.assertTrue(output.is_file())
            self.assertFalse(record.exists())

    def test_missing_h3_inputs_write_unavailable_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            campaign = pathlib.Path(directory)
            argv = ["--campaign", str(campaign), "--stage", "h3"]
            argv += ["--cell-predictions", str(campaign / "missing_cells.csv")]
            argv += ["--model-artifact", str(campaign / "missing_model.json")]
            with self.assertRaisesRegex(SystemExit, "h3 is unavailable"):
                confirm.main(argv)
            record = campaign / "analysis" / "h3_update_unavailable.json"
            self.assertIn("does not exist", json.loads(record.read_text())["reason"])

    def test_failed_h4_removes_stale_disagreement_table(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            campaign = pathlib.Path(directory)
            other = pathlib.Path(directory) / "other"
            runs = synthetic_runs(ratio=1.5, noise=0.01, trials=20, cells=11)
            timing_csv(runs, campaign / "raw" / "runs_timing-W1.csv")
            timing_csv(runs, other / "raw" / "runs_timing-W1.csv")
            stale = campaign / "analysis" / "h4_update_disagreements.csv"
            stale.parent.mkdir()
            stale.write_text("stale\n")
            argv = ["--campaign", str(campaign), "--comparison-campaign", str(other)]
            argv += ["--stage", "h4", "--h4-cells", str(campaign / "h4.csv")]
            runs[confirm.CELL_KEYS].drop_duplicates().assign(trials=20).to_csv(
                campaign / "h4.csv", index=False
            )
            with self.assertRaisesRegex(SystemExit, "h4 is unavailable"):
                confirm.main(argv)
            self.assertFalse(stale.exists())
            self.assertTrue((campaign / "analysis" / "h4_update_unavailable.json").is_file())

    def test_unavailable_record_satisfies_required_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            campaign = pathlib.Path(directory)
            analysis = campaign / "analysis"
            analysis.mkdir()
            required = ["primary_update.csv", "hierarchical_update_diagnostics.json"]
            (analysis / "hierarchical_update_diagnostics.json").write_text("[]\n")
            with self.assertRaisesRegex(SystemExit, "required primary outputs"):
                blind.hash_primary_outputs(campaign, required)
            (analysis / "primary_update_unavailable.json").write_text("{}\n")
            _, manifest = blind.hash_primary_outputs(campaign, required)
            self.assertIn(
                "primary_update_unavailable.json", [entry["file"] for entry in manifest["files"]]
            )
            (analysis / "primary_update.csv").write_text("subject\n")
            with self.assertRaisesRegex(SystemExit, "both exist"):
                blind.hash_primary_outputs(campaign, required)


class BlindTests(unittest.TestCase):
    def test_persistent_direction_rule(self) -> None:
        faster, slower = "meaningfully faster", "meaningfully slower"
        self.assertEqual(
            blind.persistent_direction("persistent", "copy-on-push", faster), "supports persistent"
        )
        self.assertEqual(
            blind.persistent_direction("copy-on-push", "persistent", slower), "supports persistent"
        )
        self.assertEqual(
            blind.persistent_direction("copy-on-push", "persistent", faster),
            "contradicts persistent",
        )
        self.assertEqual(
            blind.persistent_direction("persistent", "copy-on-push", slower),
            "contradicts persistent",
        )
        for state in ("practically equivalent", "inconclusive", ""):
            self.assertEqual(blind.persistent_direction("persistent", "copy-on-push", state), "")

    def test_contrast_labels_must_resolve_to_registered_pair(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            analysis = pathlib.Path(directory)
            pd.DataFrame({"subject": ["S01"], "baseline": ["S02"]}).to_csv(
                analysis / "primary_update.csv", index=False
            )
            pd.DataFrame({"subject": ["S01"] * 2, "baseline": ["S02", "S03"]}).to_csv(
                analysis / "regime_update_contrast-a.csv", index=False
            )
            required = ["primary_update.csv", "regime_update_contrast-a.csv"]
            self.assertEqual(blind.contrast_labels(analysis, required), ("S01", "S02"))
            pd.DataFrame({"subject": ["S04"], "baseline": ["S02"]}).to_csv(
                analysis / "mean-median_update.csv", index=False
            )
            with self.assertRaisesRegex(SystemExit, "exactly one registered contrast"):
                blind.contrast_labels(analysis, required + ["mean-median_update.csv"])

    def test_mapping_is_deterministic_and_key_dependent(self) -> None:
        first = blind.label_map(b"k" * 32)
        self.assertEqual(first, blind.label_map(b"k" * 32))
        self.assertNotEqual(first, blind.label_map(b"j" * 32))
        self.assertEqual(sorted(first.values()), [f"S{i:02d}" for i in range(1, 10)])

    def test_primary_stays_blinded_until_hash_is_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            custody = root / "custody"
            source = custody / "named"
            analyst = root / "analyst"
            (source / "raw").mkdir(parents=True)
            runs = synthetic_runs(ratio=1.2, trials=20).assign(
                update_ns=lambda frame: frame["update_ns_per_op"] * 10,
                query_ns=1000.0,
                updates=10,
                queries=10,
                batch_ns=2000.0,
                bytes=1024,
                alloc_peak_bytes=0,
                nodes=20,
                build_nodes=10,
                build_ns=100,
                status="ok",
            )
            incomplete = runs.iloc[[0]].assign(query_ns=np.nan)
            pd.concat([runs, incomplete], ignore_index=True).to_csv(
                source / "raw" / "runs_timing-W1-named.csv", index=False
            )
            blind.seal(analyst, custody, "study")
            second_analyst = root / "analyst-b"
            blind.attach(second_analyst, custody, "study")
            self.assertEqual(
                json.loads((analyst / "blinding-commitment.json").read_text())["key_sha256"],
                json.loads((second_analyst / "blinding-commitment.json").read_text())[
                    "key_sha256"
                ],
            )
            blind.blind_campaign(source, analyst, custody, "study")
            receipt = json.loads((analyst / "blinded-input-receipt.json").read_text())
            self.assertRegex(receipt["custody_manifest_sha256"], r"^[0-9a-f]{64}$")
            self.assertEqual(
                blind.verify_named_campaign(source, analyst, custody, "study"),
                receipt["custody_manifest_sha256"],
            )
            blinded = confirm.data.load_runs(analyst / "raw", "timing")
            self.assertEqual(blinded.attrs["incomplete_rows"], 1)
            confirm.assert_blinded(blinded)
            self.assertFalse(set(blinded["structure"]) & set(blind.STRUCTURES))
            key = bytes.fromhex((custody / "study" / "blind_key.hex").read_text())
            mapping = blind.label_map(key)
            label_a, label_b = sorted(
                [mapping["persistent"], mapping["copy-on-push"]]
            )
            with mock.patch.object(blind, "load_custody", side_effect=AssertionError):
                confirm.paired_cell_ratios(
                    blinded, label_a, "update_ns_per_op", [label_b]
                )

            analysis = analyst / "analysis"
            second_analysis = second_analyst / "analysis"
            analysis.mkdir()
            second_analysis.mkdir()
            for output, filenames in (
                (analysis, blind.REQUIRED_COORDINATOR_OUTPUTS),
                (second_analysis, blind.REQUIRED_LOCAL_BLINDED_OUTPUTS),
            ):
                for filename in filenames:
                    path = output / filename
                    if path.suffix == ".json":
                        path.write_text("[]\n")
                    else:
                        pd.DataFrame(
                            {"subject": [label_a], "baseline": [label_b]}
                        ).to_csv(path, index=False)
            original = blind.load_custody

            def custody_after_hash(*args, **kwargs):
                self.assertTrue((analysis / "primary-results.json").is_file())
                self.assertTrue((second_analysis / "primary-results.json").is_file())
                return original(*args, **kwargs)

            wrong_pair = analysis / "order_update.csv"
            pd.DataFrame({"subject": [label_a], "baseline": [mapping["lazy"]]}).to_csv(
                wrong_pair, index=False
            )
            with self.assertRaisesRegex(SystemExit, "exactly one registered contrast"):
                blind.unblind_study([analyst, second_analyst], custody, "study")
            pd.DataFrame(
                {
                    "subject": [label_a],
                    "baseline": [label_b],
                    "classification": ["meaningfully faster"],
                }
            ).to_csv(wrong_pair, index=False)
            with mock.patch.object(blind, "load_custody", custody_after_hash):
                records = blind.unblind_study(
                    [analyst, second_analyst], custody, "study"
                )
            self.assertEqual(len(records), 2)
            named_order = pd.read_csv(analysis / "unblinded" / "order_update.csv")
            self.assertIn(
                named_order["persistent_direction"].iloc[0],
                {"supports persistent", "contradicts persistent"},
            )
            self.assertEqual(
                named_order["persistent_direction"].iloc[0] == "supports persistent",
                named_order["subject"].iloc[0] == "persistent",
            )
            self.assertRegex(blind.verify_outputs(analyst), r"^[0-9a-f]{64}$")
            (analysis / "primary_update.csv").write_text("subject,baseline\nS09,S08\n")
            with self.assertRaisesRegex(SystemExit, "changed after it was hashed"):
                blind.verify_outputs(analyst)
            self.assertTrue(all(record.is_file() for record in records))
            unblinded = pd.read_csv(analysis / "unblinded" / "primary_update.csv")
            self.assertEqual(
                {unblinded["subject"].iloc[0], unblinded["baseline"].iloc[0]},
                {"persistent", "copy-on-push"},
            )
            first_record = json.loads(records[0].read_text())
            second_record = json.loads(records[1].read_text())
            self.assertEqual(
                first_record["study_output_sha256"],
                second_record["study_output_sha256"],
            )
            self.assertEqual(first_record["study_campaigns"], 2)


if __name__ == "__main__":
    unittest.main()
