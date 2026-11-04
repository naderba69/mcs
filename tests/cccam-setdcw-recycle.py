#!/usr/bin/env python3
"""Force an actual ECM slot reuse before a queued CW is processed.

Uses a temporary modified x64 build and synthetic clients; it ages the original
slot to avoid a 30 second wait and calls the real store_ecmdata. No production
source or configuration is modified. The aged slot must not receive the old CW.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='mcs-setdcw-recycle-') as folder:
    tmp = Path(folder)
    src = tmp / 'src'
    shutil.copytree(root / 'src', src, ignore=shutil.ignore_patterns('x64', '__pycache__'))
    (src / 'x64').mkdir()
    path = src / 'setdcw.c'
    text = path.read_text()
    marker = '\t\t\t\tecm_setdcwdata( ecm, dcw, srctype, srcid, cwmark, trace_at_queue, lifetime_at_queue );'
    assert text.count(marker) == 1
    function_marker = 'void ecm_setdcwdata( ECM_DATA *ecm'
    assert text.count(function_marker) == 1
    helper = r"""
static void test_force_recycle(ECM_DATA *ecm)
{
    pthread_mutex_lock(&prg.lockecm);
    struct cardserver_data *test_cs = ecm->cs;
    uint8_t test_ecm[MAX_ECM_SIZE];
    int test_len = ecm->ecmlen;
    uint16_t test_sid = ecm->sid, test_caid = ecm->caid;
    uint32_t test_provid = ecm->provid;
    memcpy(test_ecm, ecm->ecm, test_len);
    ecm->recvtime = GetTickCount() - TIME_ECMALIVE*2 - 100;
    test_ecm[3] ^= 0x11;
    ECM_DATA *other = store_ecmdata(test_cs, test_ecm, test_len, test_sid+1, test_caid, test_provid);
    test_ecm[3] ^= 0x22;
    ECM_DATA *reused = store_ecmdata(test_cs, test_ecm, test_len, test_sid+2, test_caid, test_provid);
    if (other == ecm || reused != ecm) abort();
    pthread_mutex_unlock(&prg.lockecm);
}
"""
    text = text.replace(function_marker, helper + function_marker)
    commit_marker = '\t\tpthread_mutex_unlock(&prg.lockecm);\n\t\treturn;\n\t}\n\n\tif (ecm->dcwstatus==STAT_DCW_SUCCESS)'
    assert text.count(commit_marker) == 1
    lock_marker = '\tpthread_mutex_lock(&prg.lockecm);\n\tecm = queued_ecm;'
    assert text.count(lock_marker) == 1
    text = text.replace(lock_marker, '    if (!strcmp(getenv("MCS_RECYCLE_WHEN"), "before-commit")) test_force_recycle(queued_ecm);\n' + lock_marker)
    injection = '    if (!strcmp(getenv("MCS_RECYCLE_WHEN"), "before-snapshot")) test_force_recycle(ecm);\n'
    after = '''
                pthread_mutex_lock(&prg.lockecm);
                FILE *f = fopen(getenv("MCS_RECYCLE_MARKER"), "w");
                if (!f) abort();
                fprintf(f, "%d %d\\n", ecm->dcwstatus == STAT_DCW_SUCCESS, memcmp(ecm->cw, dcw, 16) == 0);
                fclose(f);
                pthread_mutex_unlock(&prg.lockecm);
'''
    path.write_text(text.replace(marker, injection + marker + after))
    build = subprocess.run(['make', '-C', str(src), '-B', 'target=x64', 'link'], capture_output=True, text=True, timeout=120)
    if build.returncode:
        print(build.stdout[-4000:] + build.stderr[-4000:], file=sys.stderr)
        raise SystemExit(build.returncode)
    marker_file = tmp / 'result.txt'
    for when in ('before-snapshot', 'before-commit'):
        marker_file.unlink(missing_ok=True)
        env = dict(os.environ, MCS_TEST_RESPONSE='recycled', MCS_RECYCLE_MARKER=str(marker_file), MCS_RECYCLE_WHEN=when)
        run = subprocess.run([sys.executable, str(root / 'tests/cccam-trace-success.py'), str(src / 'x64/multics')], env=env, timeout=60)
        if marker_file.exists():
            result = marker_file.read_text().strip()
            print(when + ': reused slot success/old CW = ' + result, flush=True)
            assert result == '0 0', 'old CW was applied to a reused ECM slot'
        else:
            raise AssertionError('worker did not complete the reuse experiment')
        if run.returncode:
            raise SystemExit(run.returncode)
        print('PASS: queued CW not applied to recycled ECM slot (' + when + ')')
