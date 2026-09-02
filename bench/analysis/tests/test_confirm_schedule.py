from __future__ import annotations

import pathlib
import sys
import unittest

BENCH = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(BENCH))

import confirm_schedule  # noqa: E402


def synthetic_jobs(structures: list[str], cells: int = 1) -> list[dict]:
    jobs = []
    for cell in range(cells):
        for index, structure in enumerate(structures):
            jobs.append(
                {
                    "workload": f"W{cell + 1}",
                    "n": "1000",
                    "axis": "none",
                    "variant": "0.000000",
                    "variant_index": 0,
                    "structure": structure,
                }
            )
    return jobs


class ScheduleTests(unittest.TestCase):
    def test_coverage_every_job_gets_its_registered_trial_count(self) -> None:
        jobs = synthetic_jobs(confirm_schedule.STRUCTURES, cells=2)

        def trials_for(job: dict) -> int:
            return 3 if job["workload"] == "W1" else 5

        rows = confirm_schedule.build_schedule(jobs, trials_for, "timing", schedule_seed=7)
        self.assertEqual(len(rows), 3 * len(confirm_schedule.STRUCTURES) + 5 * len(confirm_schedule.STRUCTURES))
        for job in jobs:
            expected = trials_for(job)
            observed = [
                row
                for row in rows
                if row["workload"] == job["workload"] and row["structure"] == job["structure"]
            ]
            self.assertEqual(len(observed), expected)
            self.assertEqual(sorted(row["trial"] for row in observed), list(range(expected)))

    def test_trial_count_excludes_a_job_from_later_blocks(self) -> None:
        jobs = synthetic_jobs(["persistent", "copy-on-push"])

        def trials_for(job: dict) -> int:
            return 1 if job["structure"] == "persistent" else 4

        rows = confirm_schedule.build_schedule(jobs, trials_for, "timing", schedule_seed=1)
        persistent_trials = {row["trial"] for row in rows if row["structure"] == "persistent"}
        copy_trials = {row["trial"] for row in rows if row["structure"] == "copy-on-push"}
        self.assertEqual(persistent_trials, {0})
        self.assertEqual(copy_trials, {0, 1, 2, 3})

    def test_within_block_order_is_a_full_permutation(self) -> None:
        jobs = synthetic_jobs(confirm_schedule.STRUCTURES)
        rows = confirm_schedule.build_schedule(jobs, lambda job: 5, "timing", schedule_seed=42)
        for trial in range(5):
            block = [row for row in rows if row["trial"] == trial]
            self.assertEqual(
                sorted(row["exec_order"] for row in block), list(range(len(confirm_schedule.STRUCTURES)))
            )

    def test_balance_no_structure_always_leads_its_block(self) -> None:
        jobs = synthetic_jobs(confirm_schedule.STRUCTURES)
        rows = confirm_schedule.build_schedule(jobs, lambda job: 20, "timing", schedule_seed=20270214)
        leaders = {row["structure"] for row in rows if row["exec_order"] == 0}
        self.assertGreater(
            len(leaders), 1, "the registered schedule seed shuffles every trial block"
        )

    def test_global_sequence_is_monotonic_and_unique(self) -> None:
        jobs = synthetic_jobs(confirm_schedule.STRUCTURES, cells=3)
        rows = confirm_schedule.build_schedule(jobs, lambda job: 4, "timing", schedule_seed=5)
        sequence = [row["process_seq"] for row in rows]
        self.assertEqual(sequence, sorted(sequence))
        self.assertEqual(len(set(sequence)), len(sequence))
        self.assertEqual(sequence, list(range(len(rows))))

    def test_determinism_same_seed_same_schedule(self) -> None:
        jobs = synthetic_jobs(confirm_schedule.STRUCTURES, cells=2)
        first = confirm_schedule.build_schedule(jobs, lambda job: 6, "timing", schedule_seed=99)
        second = confirm_schedule.build_schedule(jobs, lambda job: 6, "timing", schedule_seed=99)
        self.assertEqual(first, second)

    def test_different_seed_reorders_blocks(self) -> None:
        jobs = synthetic_jobs(confirm_schedule.STRUCTURES)
        first = confirm_schedule.build_schedule(jobs, lambda job: 3, "timing", schedule_seed=1)
        second = confirm_schedule.build_schedule(jobs, lambda job: 3, "timing", schedule_seed=2)
        self.assertNotEqual(
            [row["structure"] for row in first], [row["structure"] for row in second]
        )

    def test_write_schedule_round_trips_as_tab_separated(self) -> None:
        import tempfile

        jobs = synthetic_jobs(["persistent", "copy-on-push"])
        rows = confirm_schedule.build_schedule(jobs, lambda job: 2, "timing", schedule_seed=3)
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory) / "schedule.tsv"
            confirm_schedule.write_schedule(rows, out)
            lines = out.read_text().splitlines()
            self.assertEqual(
                lines[0].removeprefix("# ").split("\t"), confirm_schedule.SCHEDULE_COLUMNS
            )
            self.assertEqual(len(lines) - 1, len(rows))
            first = dict(zip(confirm_schedule.SCHEDULE_COLUMNS, lines[1].split("\t")))
            self.assertEqual(first["structure"], str(rows[0]["structure"]))
            self.assertEqual(first["exec_order"], str(rows[0]["exec_order"]))


if __name__ == "__main__":
    unittest.main()
