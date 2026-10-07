"""Check ActionInput guard variants after region retention repair."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parent / "elisa-engine-proof/build/elisa-proof"
SOURCE = ROOT / "src/runtime/action_input.elisa"
base = SOURCE.read_text().replace(
    'include "../vendor/elisa_math.elisa"',
    'include "' + str(ROOT / "src/vendor/elisa_math.elisa") + '"',
)
start = base.index("            if slot == Limits::MAX_ACTIONS:\n", base.index("def bind_checked"))
end = base.index("            region binding_slot_scope:", start)
without_search = base[:start] + base[end:]
call = "slot: mutable usize = state_slot(input, binding.action)"
variants = (
    ("original", base, 0, 0),
    ("no-search", without_search, 3, 0),
    ("literal-call", base.replace(call, "slot: mutable usize = 0"), 0, 0),
    ("literal-no-search", without_search.replace(call, "slot: mutable usize = 0"), 0, 3),
)
with tempfile.TemporaryDirectory(prefix="elisa-input-guard-") as directory:
    for name, source, findings, gaps in variants:
        path = Path(directory) / (name + ".elisa")
        path.write_text(source)
        run = subprocess.run([str(BINARY), "--function-json", "bind_checked", str(path)],
                             capture_output=True, text=True, timeout=60)
        report = json.loads(run.stdout)
        assert run.returncode == (1 if findings or gaps else 0), (name, report["summary"])
        assert report["summary"]["semantic_errors"] == 0
        assert report["summary"]["finding_count"] == findings, (name, report["summary"])
        assert report["replay"]["gaps"] == gaps, (name, report["replay"])
        assert not report["trust"]["trusted_assumptions"]
        binding_goals = [goal for goal in report["goals"] if goal["rule"] == "index-upper"
                         and goal["goal"].get("left", {}).get("name") == "binding_slot"]
        assert binding_goals and all(goal["proven"] for goal in binding_goals), name
        print(name, report["summary"], report["replay"])
