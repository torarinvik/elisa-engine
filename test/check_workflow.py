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
reference = '#!/bin/bash\nset -euo pipefail\nENGINE_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"\nCOMPILER="${ELISA_COMPILER_BIN:-$(command -v elisac-stage1 || true)}"\nPROVER="${ELISA_PROOF_BIN:-$ENGINE_ROOT/../elisa-proof/build/elisa-proof}"\nif [[ -z "$COMPILER" || ! -x "$COMPILER" ]]; then\n    printf \'Put elisac-stage1 on PATH or set ELISA_COMPILER_BIN to its executable path.\\n\' >&2\n    exit 2\nfi\nif [[ ! -x "$PROVER" ]]; then\n    printf \'Build elisa-proof or set ELISA_PROOF_BIN to its executable path.\\n\' >&2\n    exit 2\nfi\nmkdir -p "$ENGINE_ROOT/build"\n"$COMPILER" -emit exe -o "$ENGINE_ROOT/build/entity-id-test" "$ENGINE_ROOT/test/entity_id.elisa"\n"$ENGINE_ROOT/build/entity-id-test"\nprintf \'Entity identity runtime tests passed.\\n\'\n"$PROVER" "$ENGINE_ROOT/proof/entity_id.elisa"\n"$PROVER" --json "$ENGINE_ROOT/proof/entity_id.elisa" > "$ENGINE_ROOT/build/entity-id-proof.json"\n'
with tempfile.TemporaryDirectory(prefix='engine script parity ') as td:
    root = pathlib.Path(td)
    project = root / 'engine space'
    (project / 'scripts').mkdir(parents=True)
    (project / 'test').mkdir()
    (project / 'proof').mkdir()
    (project / 'test/entity_id.elisa').touch()
    (project / 'proof/entity_id.elisa').touch()
    (project / 'scripts/check.sh').write_text(reference)
    shutil.copyfile(source, project / 'scripts/check.elisascript')
    tools = root / 'tool space'
    tools.mkdir()
    comp = tools / 'elisac-stage1'
    proof = tools / 'elisa-proof'
    comp.write_text('#!/bin/sh\nprintf \'compiler diagnostic\\n\' >&2\nprintf \'compile\\n\' >> "$CHECK_TRACE"\n[ "${CHECK_FAIL:-}" = compile ] && exit 7\n[ "$#" = 5 ] || exit 99\nprintf \'#!/bin/sh\\nprintf "test\\\\n" >> "$CHECK_TRACE"\\n[ "${CHECK_FAIL:-}" = test ] && exit 8\\nexit 0\\n\' > "$4"\nchmod +x "$4"\n')
    comp.chmod(0o755)
    proof.write_text('#!/bin/sh\nprintf \'prover diagnostic\\n\' >&2\nif [ "$1" = --json ]; then\n printf \'json\\n\' >> "$CHECK_TRACE"\n printf \'{"verification_state":"proved"}\\n\'\n [ "${CHECK_FAIL:-}" = json ] && exit 10\nelse\n printf \'proof\\n\' >> "$CHECK_TRACE"\n printf \'proof passed\\n\'\n [ "${CHECK_FAIL:-}" = proof ] && exit 9\nfi\nexit 0\n')
    proof.chmod(0o755)
    default_prover = root / 'elisa-proof/build/elisa-proof'
    default_prover.parent.mkdir(parents=True)
    shutil.copy2(proof, default_prover)
    cases = ['success', 'compile', 'test', 'proof', 'json', 'missing-compiler', 'missing-prover', 'empty-overrides', 'path-lookup', 'default-prover']
    all_results = []
    for case in cases:
        paired = []
        for mode in ['shell', 'elisascript']:
            shutil.rmtree(project / 'build', ignore_errors=True)
            trace = root / 'trace'
            trace.write_text('')
            env = os.environ.copy()
            env.update(ELISA_COMPILER_BIN=str(comp), ELISA_PROOF_BIN=str(proof), CHECK_FAIL=case, CHECK_TRACE=str(trace))
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
        expected_status = {'success': 0, 'compile': 7, 'test': 8, 'proof': 9, 'json': 10, 'missing-compiler': 2, 'missing-prover': 2, 'empty-overrides': 0, 'path-lookup': 0, 'default-prover': 0}[case]
        assert paired[0]['status'] == expected_status, ('broken shell oracle', case, paired[0])
        passed = paired[0] == paired[1]
        all_results.append(dict(case=case, passed=passed, results=paired))
        print(case, 'PASS' if passed else json.dumps(paired))
    (engine / 'build/script-validation').mkdir(parents=True, exist_ok=True)
    (engine / 'build/script-validation/parity-results.json').write_text(json.dumps(all_results, indent=2))
    sys.exit(0 if all((r['passed'] for r in all_results)) else 1)
