from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

import numpy as np
import pandas as pd

ANALYSIS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ANALYSIS))

import cost_model  # noqa: E402
import split  # noqa: E402


class SplitTests(unittest.TestCase):
    def structural_rows(self) -> pd.DataFrame:
        return pd.DataFrame(
            [
                ("W4", 10000, "none", 0.0, 11, "same-11"),
                ("W4", 10000, "none", 0.0, 12, "same-12"),
                ("W11", 10000, "width", 1.0, 11, "same-11"),
                ("W11", 10000, "width", 1.0, 12, "same-12"),
                ("W3", 10000, "none", 0.0, 11, "other-11"),
                ("W3", 10000, "none", 0.0, 12, "other-12"),
            ],
            columns=["workload", "n", "axis", "variant", "seed", "stream_fingerprint"],
        )

    def test_identical_stream_families_share_partition(self) -> None:
        grouped = split.add_stream_groups(self.structural_rows())
        assigned = split.assign(grouped, group_column="stream_group")

        w4 = assigned[assigned["workload"] == "W4"]
        w11 = assigned[assigned["workload"] == "W11"]
        self.assertEqual(w4["stream_group"].iloc[0], w11["stream_group"].iloc[0])
        self.assertEqual(w4["partition"].iloc[0], w11["partition"].iloc[0])
        self.assertEqual(w4["partition"].nunique(), 1)

    def test_conflicting_fingerprint_for_seed_is_rejected(self) -> None:
        rows = self.structural_rows()
        duplicate = rows.iloc[[0]].copy()
        duplicate["stream_fingerprint"] = "conflict"
        with self.assertRaisesRegex(ValueError, "conflicting stream fingerprints"):
            split.add_stream_groups(pd.concat([rows, duplicate], ignore_index=True))


class CostModelTests(unittest.TestCase):
    def model_rows(self) -> pd.DataFrame:
        rows = []
        for index in range(24):
            training = index < 16
            visits = float(index % 8 + 1)
            rows.append(
                {
                    "workload": f"W{index % 4 + 1}",
                    "n": 1000 * (index % 3 + 1),
                    "axis": "none",
                    "variant": float(index % 2),
                    "seed": 100 + index,
                    "structure": "persistent",
                    "partition": "training" if training else "holdout",
                    "trial": index,
                    "stream_group": f"group-{index}",
                    "updates": 10,
                    "queries": 10,
                    "sum_update_visits": visits * 10,
                    "sum_checkpoint_update_visits": 0,
                    "sum_intersecting": visits * 20,
                    "sum_query_visits": visits * 10,
                    "sum_replay_entries": 0,
                    "sum_query_version_distance": index * 10,
                    "full_coverage_updates": index % 3,
                    "full_coverage_queries": index % 2,
                    "nonzero_updates": 10,
                    "nodes_per_update": visits,
                    "bytes": 4096 * (index + 1),
                    "update_ns_per_op": 10.0 + 2.0 * visits,
                    "query_ns_per_op": 12.0 + 3.0 * visits,
                }
            )
        return pd.DataFrame(rows)

    def test_fit_drops_constant_and_collinear_predictors(self) -> None:
        frame = pd.DataFrame(
            {
                "visited_records": [1.0, 2.0, 3.0, 4.0],
                "allocated_records": [1.0, 2.0, 3.0, 4.0],
                "working_set_transition": [0.0, 1.0, 0.0, 1.0],
                "version_distance_transition": [0.0, 0.0, 0.0, 0.0],
                "full_coverage_share": [0.0, 0.0, 1.0, 1.0],
                "fit_response": np.log([10.0, 20.0, 40.0, 80.0]),
            }
        )
        coefficients, kept = cost_model.fit(frame, cost_model.CANDIDATE_COLUMNS)

        self.assertNotIn("visited_records", kept)
        self.assertNotIn("version_distance_transition", kept)
        predicted = cost_model.predict(frame, coefficients, kept)
        self.assertTrue(np.all(predicted > 0))

    def test_nonpositive_predictions_are_counted_as_errors(self) -> None:
        result = cost_model.errors(
            np.asarray([10.0, 20.0, 40.0]), np.asarray([8.0, 0.0, -4.0])
        )
        self.assertEqual(result["rows"], 3)
        self.assertEqual(result["nonpositive_predictions"], 2)
        self.assertGreaterEqual(result["mape_p90"], 100.0)

    def test_model_artifact_round_trips_with_stable_hash(self) -> None:
        value = cost_model.artifact(
            4096,
            [
                {
                    "op": "query",
                    "structure": "subject",
                    "columns": ["visited_records"],
                    "coefficients": [1.0, 2.0],
                    "training_cells": 2,
                    "training_rows": 16,
                }
            ],
            "test-salt",
            0.25,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "model.json"
            written_hash = cost_model.write_artifact(path, value)
            loaded, read_hash = cost_model.read_artifact(path)

        self.assertEqual(loaded, value)
        self.assertEqual(read_hash, written_hash)

    def test_fit_is_independent_of_holdout_responses(self) -> None:
        rows = self.model_rows()
        first = cost_model.fit_models(rows, 4096)
        rows.loc[rows["partition"] == "holdout", "update_ns_per_op"] = 10**9
        rows.loc[rows["partition"] == "holdout", "query_ns_per_op"] = 10**9
        second = cost_model.fit_models(rows, 4096)

        self.assertEqual(first, second)

    def test_missing_structural_directory_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(SystemExit):
                cost_model.load_structural(pathlib.Path(directory))

    def test_incomplete_structural_schema_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "structural_test-W1.csv"
            pd.DataFrame({"workload": ["W1"]}).to_csv(path, index=False)
            with self.assertRaisesRegex(SystemExit, "structural counts missing columns"):
                cost_model.load_structural(pathlib.Path(directory))


if __name__ == "__main__":
    unittest.main()
