"""Controls for complete VM-map comparison without relaxing the lifecycle gate."""
import unittest
from effect_memory_region_report import analyze


def fixture():
    lines = []
    for cycle in range(7):
        footprint = 20000 if cycle == 3 else 10000
        lines += [f"effect memory cycle {cycle}: phys_footprint {footprint} KiB",
            f"effect memory VM regions cycle {cycle}: complete 1 status 1 regions 2",
            f"effect memory VM tag cycle {cycle}: tag 1 virtual 100 resident {200 if cycle == 3 else 100} dirtied 50 regions 1",
            f"effect memory VM tag cycle {cycle}: tag 2 virtual 100 resident {50 if cycle == 3 else 100} dirtied 50 regions 1"]
    return "\n".join(lines)


class RegionReportTests(unittest.TestCase):
    def test_peak_and_signed_tag_deltas(self):
        report = analyze(fixture())
        self.assertEqual(report["peak_cycle"], 3)
        self.assertEqual(report["peak_growth_kib"], 10000)
        self.assertTrue(report["over_allowance"])
        self.assertEqual([row["resident"] for row in report["peak_tag_deltas"]], [100, -50])

    def test_missing_partial_duplicate_and_truncated_rows_refused(self):
        original = fixture()
        for broken in (original.replace("complete 1", "complete 0", 1),
                original + "\n" + original.splitlines()[0],
                original.replace("status 1 regions 2", "status 1 regions 3", 1),
                "\n".join(original.splitlines()[:-1]),
                original.replace("tag 1 virtual", "tag 900 virtual", 1)):
            with self.subTest(broken=broken[:100]), self.assertRaises(ValueError):
                analyze(broken)

    def test_original_allowance_boundary(self):
        report = analyze(fixture().replace("phys_footprint 20000", "phys_footprint 18192"))
        self.assertFalse(report["over_allowance"])
        self.assertEqual(report["allowance_kib"], 8192)


if __name__ == "__main__":
    unittest.main()
