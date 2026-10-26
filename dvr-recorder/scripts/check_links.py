#!/usr/bin/env python3
"""Offline stand-in for the board's dynamic loader.

Loads a program's DT_NEEDED libraries (and theirs, breadth first) from the board's
libs and the vendor libs, exactly as /lib/ld-uClibc.so.0 would find them, then
reports every non-weak undefined symbol that no loaded object defines.

    python scripts/check_links.py build/dvrtest
"""
import os
import sys

from elftools.elf.elffile import ELFFile

HERE = os.path.dirname(os.path.abspath(__file__))

# Our toolchain's uClibc-ng uses 64-bit time_t, the board's uClibc 0.9.33 uses 32-bit:
# these calls pass time structs with a different layout, so our programs must not import
# them (see src/compat.h). Vendor libraries are built for the board and are not checked.
TIME_ABI = {'gettimeofday', 'settimeofday', 'select', 'pselect', 'nanosleep', 'clock_gettime',
            'clock_settime', 'clock_nanosleep', 'time', 'stat', 'fstat', 'lstat', 'utime', 'utimes',
            'localtime', 'localtime_r', 'gmtime', 'gmtime_r', 'mktime', 'strftime', 'difftime',
            'ctime', 'timer_settime', 'timerfd_settime', 'setitimer', 'getitimer', 'sigtimedwait',
            'futimes', 'ppoll', 'getrusage', 'wait3', 'wait4', 'adjtime', 'pthread_cond_timedwait',
            'pthread_mutex_timedlock', 'sem_timedwait', 'mq_timedreceive', 'mq_timedsend'}
SEARCH = [os.path.join(HERE, '..', 'vendor', 'lib'), os.path.join(HERE, '..', 'sysroot-board', 'lib')]


def load(path):
    with open(path, 'rb') as f:
        e = ELFFile(f)
        needed = [t.needed for t in e.get_section_by_name('.dynamic').iter_tags()
                  if t.entry.d_tag == 'DT_NEEDED']
        defined, undefined = set(), set()
        for s in e.get_section_by_name('.dynsym').iter_symbols():
            if not s.name:
                continue
            if s['st_shndx'] == 'SHN_UNDEF':
                if s['st_info']['bind'] != 'STB_WEAK':
                    undefined.add(s.name)
            else:
                defined.add(s.name)
    return needed, defined, undefined


def find(name):
    for d in SEARCH:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


def main(prog):
    objs, queue, seen, missing_libs = {}, [prog], set(), []
    while queue:
        p = queue.pop(0)
        objs[p] = load(p)
        for n in objs[p][0]:
            if n in seen:
                continue
            seen.add(n)
            q = find(n)
            if q:
                queue.append(q)
            else:
                missing_libs.append(n)
    scope = set().union(*(o[1] for o in objs.values()))
    ok = not missing_libs
    bad_time = sorted(objs[prog][2] & TIME_ABI)
    if bad_time:
        ok = False
        print('TIME ABI: %s imports %s (64-bit time_t here, 32-bit on the board; use src/compat.h)'
              % (os.path.basename(prog), ', '.join(bad_time)))
    print('loaded:', ', '.join(os.path.basename(p) for p in objs))
    if missing_libs:
        print('MISSING LIBRARIES:', missing_libs)
    for p, (_, _, und) in objs.items():
        miss = sorted(und - scope)
        if miss:
            ok = False
            print('UNRESOLVED in %s: %s' % (os.path.basename(p), ', '.join(miss)))
    print('OK: every symbol resolves' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
