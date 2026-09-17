"""Compare the ElisaScript check against its original shell workflow.

Run from any directory with the native launcher path as the sole argument.
Fake tools exercise success, failure propagation, paths with spaces, discovery,
and proof-report persistence without rebuilding either actual toolchain.
"""
import os, sys, tempfile, pathlib, subprocess, json, shutil
if len(sys.argv) != 2:
    raise SystemExit('usage: python3 test/check_workflow.py /path/to/elisascript')
launcher = pathlib.Path(sys.argv[1]).resolve()
engine = pathlib.Path(__file__).resolve().parents[1]
source = engine / 'scripts/check.elisascript'
reference = '''#!/bin/bash
set -euo pipefail
ENGINE_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${ELISA_COMPILER_BIN:-$(command -v elisac-stage1 || true)}"
PROVER="${ELISA_PROOF_BIN:-$ENGINE_ROOT/../elisa-proof/build/elisa-proof}"
if [[ -z "$COMPILER" || ! -x "$COMPILER" ]]; then
    printf 'Put elisac-stage1 on PATH or set ELISA_COMPILER_BIN to its executable path.\n' >&2
    exit 2
fi
if [[ ! -x "$PROVER" ]]; then
    printf 'Build elisa-proof or set ELISA_PROOF_BIN to its executable path.\n' >&2
    exit 2
fi
GODOT="${GODOT_BIN:-$(command -v godot || true)}"
if [[ -z "$GODOT" || ! -x "$GODOT" ]]; then
    printf 'Install Godot or set GODOT_BIN to its executable path.\n' >&2
    exit 2
fi
mkdir -p "$ENGINE_ROOT/build"
rm -f "$ENGINE_ROOT/build/entity-id-proof.json"
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/entity-id-test" "$ENGINE_ROOT/test/entity_id.elisa"
"$ENGINE_ROOT/build/entity-id-test"
printf 'Entity identity runtime tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/world-test" "$ENGINE_ROOT/test/world.elisa"
"$ENGINE_ROOT/build/world-test"
printf 'Checked world lifecycle tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/geometry-test" "$ENGINE_ROOT/test/geometry.elisa"
"$ENGINE_ROOT/build/geometry-test"
printf 'Geometry value tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/assets-test" "$ENGINE_ROOT/test/assets.elisa"
"$ENGINE_ROOT/build/assets-test"
printf 'Portable asset descriptor tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/input-test" "$ENGINE_ROOT/test/input.elisa"
"$ENGINE_ROOT/build/input-test"
printf 'Portable input mapping tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/capabilities-test" "$ENGINE_ROOT/test/capabilities.elisa"
"$ENGINE_ROOT/build/capabilities-test"
printf 'Backend capability tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/fake-bridge-test" "$ENGINE_ROOT/test/fake_bridge.elisa"
"$ENGINE_ROOT/build/fake-bridge-test"
printf 'Fake native bridge lifecycle tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/sdl3-test" -L /opt/homebrew/lib -l SDL3 "$ENGINE_ROOT/test/sdl3.elisa"
"$ENGINE_ROOT/build/sdl3-test"
printf 'SDL3 platform probe passed.\n'
"$GODOT" --headless --path "$ENGINE_ROOT/backends/godot" --script "$ENGINE_ROOT/backends/godot/probe.gd" -- "$ENGINE_ROOT/backends/scene_manifest.txt"
printf 'Godot backend scene probe passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/contracts-test" "$ENGINE_ROOT/test/contracts.elisa"
"$ENGINE_ROOT/build/contracts-test"
printf 'FFI ownership contract tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/recording-test" "$ENGINE_ROOT/test/recording.elisa"
"$ENGINE_ROOT/build/recording-test"
printf 'Recording backend command tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/clock-test" "$ENGINE_ROOT/test/clock.elisa"
"$ENGINE_ROOT/build/clock-test"
printf 'Fixed-step clock tests passed.\n'
"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/headless-game-test" "$ENGINE_ROOT/test/headless_game.elisa"
"$ENGINE_ROOT/build/headless-game-test"
printf 'Deterministic headless game tests passed.\n'
rejects_ownership_copy() {
    local diagnostic status=0
    diagnostic="$("$COMPILER" -emit obj -o "$ENGINE_ROOT/build/ownership-negative.o" "$1" 2>&1)" || status=$?
    if [[ "$status" == 0 ]]; then
        printf 'An affine owner was copied without a compiler error.\n' >&2
        return 1
    fi
    if [[ "$diagnostic" != *'linear value'* ]]; then
        printf '%s\n' "$diagnostic" >&2
        printf 'The negative ownership fixture failed for another reason.\n' >&2
        return 1
    fi
}
rejects_ownership_copy "$ENGINE_ROOT/test/negative/allocator_copy.elisa"
rejects_ownership_copy "$ENGINE_ROOT/test/negative/world_copy.elisa"
rejects_ownership_copy "$ENGINE_ROOT/test/negative/recorder_copy.elisa"
rejects_ownership_copy "$ENGINE_ROOT/test/negative/bridge_copy.elisa"
printf 'Affine owner copy rejection tests passed.\n'
"$PROVER" "$ENGINE_ROOT/proof/entity_id.elisa"
"$PROVER" "$ENGINE_ROOT/proof/world.elisa"
"$PROVER" --json "$ENGINE_ROOT/proof/entity_id.elisa" > "$ENGINE_ROOT/build/entity-id-proof.json"
"$PROVER" --json "$ENGINE_ROOT/proof/world.elisa" > "$ENGINE_ROOT/build/world-proof.json"
python3 "$ENGINE_ROOT/scripts/record_validation.py" "$ENGINE_ROOT" "$COMPILER" "$PROVER" "elisascript"
'''
with tempfile.TemporaryDirectory(prefix='engine script parity ') as td:
    root = pathlib.Path(td)
    project = root / 'engine space'
    (project / 'scripts').mkdir(parents=True)
    (project / 'test').mkdir()
    (project / 'test/negative').mkdir()
    (project / 'proof').mkdir()
    (project / 'test/entity_id.elisa').touch()
    (project / 'test/world.elisa').touch()
    (project / 'test/geometry.elisa').touch()
    (project / 'test/assets.elisa').touch()
    (project / 'test/input.elisa').touch()
    (project / 'test/capabilities.elisa').touch()
    (project / 'test/sdl3.elisa').touch()
    (project / 'test/fake_bridge.elisa').touch()
    (project / 'test/contracts.elisa').touch()
    (project / 'test/recording.elisa').touch()
    (project / 'test/clock.elisa').touch()
    (project / 'test/headless_game.elisa').touch()
    (project / 'test/negative/allocator_copy.elisa').touch()
    (project / 'test/negative/world_copy.elisa').touch()
    (project / 'test/negative/recorder_copy.elisa').touch()
    (project / 'test/negative/bridge_copy.elisa').touch()
    (project / 'proof/entity_id.elisa').touch()
    (project / 'proof/world.elisa').touch()
    (project / 'scripts/check.sh').write_text(reference)
    shutil.copyfile(source, project / 'scripts/check.elisascript')
    (project / 'scripts/record_validation.py').write_text(
        'import json, pathlib, sys\n'
        'root = pathlib.Path(sys.argv[1])\n'
        'if __import__("os").environ.get("CHECK_FAIL") == "validation":\n'
        '    raise SystemExit(12)\n'
        '(root / "build/validation.json").write_text(json.dumps({"status": "passed"}) + "\\n")\n'
        'print("Validation report written.")\n'
    )
    tools = root / 'tool space'
    tools.mkdir()
    comp = tools / 'elisac-stage1'
    proof = tools / 'elisa-proof'
    comp.write_text('#!/bin/sh\ncase "$5" in\n  */test/negative/*)\n    printf \'negative\\n\' >> "$CHECK_TRACE"\n    [ "${CHECK_FAIL:-}" = negative-accepted ] && exit 0\n    if [ "${CHECK_FAIL:-}" = negative-wrong-diagnostic ]; then\n      printf \'unrelated compiler failure\\n\' >&2\n    else\n      printf \'linear value cannot be copied\\n\' >&2\n    fi\n    exit 1\n    ;;\nesac\nprintf \'compiler diagnostic\\n\' >&2\nprintf \'compile\\n\' >> "$CHECK_TRACE"\n[ "${CHECK_FAIL:-}" = compile ] && exit 7\n[ "$#" -ge 5 ] || exit 99\nprintf \'#!/bin/sh\\nprintf "test\\\\n" >> "$CHECK_TRACE"\\n[ "${CHECK_FAIL:-}" = test ] && exit 8\\nexit 0\\n\' > "$4"\nchmod +x "$4"\n')
    comp.chmod(0o755)
    proof.write_text('#!/bin/sh\nprintf \'prover diagnostic\\n\' >&2\nif [ "$1" = --json ]; then\n printf \'json\\n\' >> "$CHECK_TRACE"\n printf \'{"verification_state":"proved"}\\n\'\n [ "${CHECK_FAIL:-}" = json ] && exit 10\nelse\n printf \'proof\\n\' >> "$CHECK_TRACE"\n printf \'proof passed\\n\'\n [ "${CHECK_FAIL:-}" = proof ] && exit 9\nfi\nexit 0\n')
    proof.chmod(0o755)
    godot = tools / 'godot'
    godot.write_text("#!/bin/sh\nprintf 'godot\n' >> \"$CHECK_TRACE\"\n[ \"${CHECK_FAIL:-}\" = godot ] && exit 6\nexit 0\n")
    godot.chmod(0o755)
    default_prover = root / 'elisa-proof/build/elisa-proof'
    default_prover.parent.mkdir(parents=True)
    shutil.copy2(proof, default_prover)
    cases = ['success', 'compile', 'test', 'proof', 'json', 'validation', 'godot', 'stale-report', 'negative-accepted', 'negative-wrong-diagnostic', 'missing-compiler', 'missing-prover', 'empty-overrides', 'path-lookup', 'default-prover']
    all_results = []
    for case in cases:
        paired = []
        for mode in ['shell', 'elisascript']:
            shutil.rmtree(project / 'build', ignore_errors=True)
            if case == 'stale-report':
                (project / 'build').mkdir()
                (project / 'build/entity-id-proof.json').write_text('{"verification_state":"proved"}\n')
            trace = root / 'trace'
            trace.write_text('')
            env = os.environ.copy()
            env.update(ELISA_COMPILER_BIN=str(comp), ELISA_PROOF_BIN=str(proof), CHECK_FAIL='compile' if case == 'stale-report' else case, CHECK_TRACE=str(trace))
            env['PATH'] = str(tools) + ':' + env.get('PATH', '')
            if case == 'missing-compiler':
                env['ELISA_COMPILER_BIN'] = str(root / 'missing')
            if case == 'missing-prover':
                env['ELISA_PROOF_BIN'] = str(root / 'missing')
            if case == 'path-lookup':
                env.pop('ELISA_COMPILER_BIN')
            if case == 'empty-overrides':
                env['ELISA_COMPILER_BIN'] = ''
                env['ELISA_PROOF_BIN'] = ''
            if case == 'default-prover':
                env.pop('ELISA_PROOF_BIN')
            command = ['/bin/bash', str(project / 'scripts/check.sh')] if mode == 'shell' else [str(launcher), str(project / 'scripts/check.elisascript')]
            run = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True, timeout=30)
            report = project / 'build/entity-id-proof.json'
            paired.append(dict(status=run.returncode, stdout=run.stdout, stderr=run.stderr, trace=trace.read_text(), report=report.read_text() if report.exists() else None))
        expected_status = {'success': 0, 'compile': 7, 'test': 8, 'proof': 9, 'json': 10, 'validation': 12, 'godot': 6, 'stale-report': 7, 'negative-accepted': 1, 'negative-wrong-diagnostic': 1, 'missing-compiler': 2, 'missing-prover': 2, 'empty-overrides': 0, 'path-lookup': 0, 'default-prover': 0}[case]
        assert paired[0]['status'] == expected_status, ('broken shell oracle', case, paired[0])
        if case == 'stale-report':
            assert paired[0]['report'] is None, ('stale shell report', paired[0])
        passed = paired[0] == paired[1]
        all_results.append(dict(case=case, passed=passed, results=paired))
        print(case, 'PASS' if passed else json.dumps(paired))
    (engine / 'build/script-validation').mkdir(parents=True, exist_ok=True)
    (engine / 'build/script-validation/parity-results.json').write_text(json.dumps(all_results, indent=2))
    sys.exit(0 if all((r['passed'] for r in all_results)) else 1)
