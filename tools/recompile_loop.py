#!/usr/bin/env python3
"""Recompile, scan for functions the recompiler missed, and repeat until none are left.
Then fix jump tables the recompiler cut short (tools/fix_switch_tables.py) and repeat again.

Usage: recompile_loop.py <rexglue.exe> <project dir> <logs dir>
"""
import glob
import os
import re
import subprocess
import sys

rexglue, proj, logs = sys.argv[1:4]
here = os.path.dirname(os.path.abspath(__file__))
manifest = glob.glob(os.path.join(proj, '*_manifest.toml'))[0]
# The manifest includes <name>_functions.toml (missed functions) and <name>_switches.toml
# (jump table fixes). Older projects only list the first one, so add the second.
mtext = open(manifest).read()
m = re.search(r'includes\s*=\s*\[([^\]]*)\]', mtext)
names = re.findall(r'"([^"]+)"', m.group(1))
fname = next(n for n in names if n.endswith('_functions.toml'))
sname = fname.replace('_functions.toml', '_switches.toml')
if sname not in names:
    names.append(sname)
    mtext = mtext[:m.start()] + 'includes = [' + ', '.join(f'"{n}"' for n in names) + ']' + mtext[m.end():]
    open(manifest, 'w').write(mtext)
functions = os.path.join(proj, fname)
switches = os.path.join(proj, sname)
if not os.path.exists(switches):
    open(switches, 'w').close()
xex = os.path.join(proj, 'input', 'default.xex')
gen = os.path.join(proj, 'generated', 'default')
log = os.path.join(logs, '2_codegen.log')

for n in range(1, 13):
    for stamp in ('codegen.stamp', 'codegen.build.stamp'):
        try:
            os.remove(os.path.join(gen, stamp))
        except OSError:
            pass
    print(f'  Pass {n}: recompiling...', flush=True)
    proc = subprocess.Popen([rexglue, 'codegen', os.path.basename(manifest), '--ignore-stamp',
                             '--log-file', log], cwd=proj, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace')
    out = []
    for line in proc.stdout:
        print(line, end='', flush=True)
        out.append(line)
    proc.wait()
    if proc.returncode != 0:
        # Branches into code that isn't part of any function yet: add each target as a
        # function and recompile. Anything else is a real failure.
        targets = sorted(set(re.findall(r'\bbl? 0x([0-9A-Fa-f]{8}) from 0x[0-9A-Fa-f]{8}', ''.join(out))))
        if not targets:
            print('  Recompile failed - see logs\\2_codegen.log')
            sys.exit(1)
        text = open(functions).read().rstrip('\r\n') + '\n'
        for t in targets:
            if f'0x{t.upper()} =' not in text:
                text += f'0x{t.upper()} = {{}}\n'
        with open(functions, 'w', newline='\r\n') as f:
            f.write(text)
        print(f'  Pass {n}: added {len(targets)} branch target(s) as functions, recompiling again')
        continue
    print(f'  Pass {n}: looking for missed functions...', flush=True)
    s = subprocess.run([sys.executable, '-I', os.path.join(here, 'find_missing_functions.py'), gen,
                        functions], capture_output=True, text=True)
    if s.returncode != 0:
        print(s.stderr)
        sys.exit(1)
    counts = re.search(r'(\d+) new, (\d+) dropped, (\d+) total', s.stderr)
    new, dropped, total = (int(x) for x in counts.groups())
    print(f'  Pass {n}: {new} new, {dropped} dropped ({total} listed)', flush=True)
    with open(functions, 'w', newline='\r\n') as f:
        f.write(s.stdout)
    if new or dropped:
        continue
    print(f'  Pass {n}: checking jump tables...', flush=True)
    s = subprocess.run([sys.executable, '-I', os.path.join(here, 'fix_switch_tables.py'), xex, gen,
                        switches], capture_output=True, text=True)
    print(s.stderr, end='', flush=True)
    if s.returncode != 0:
        sys.exit(1)
    fixed = int(re.search(r'(\d+) switch tables fixed', s.stderr).group(1))
    if fixed == 0:
        print('  No missed functions or cut-short jump tables left.')
        s = subprocess.run([sys.executable, '-I', os.path.join(here, 'patch_generated.py'), gen],
                           capture_output=True, text=True)
        print(s.stderr, end='', flush=True)
        sys.exit(0 if s.returncode in (0, 3) else 1)
    print(f'  Pass {n}: fixed {fixed} jump table(s), recompiling again', flush=True)
print('  Stopped after 12 passes; run it again if the game still reports missing functions.')
