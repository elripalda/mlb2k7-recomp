#!/usr/bin/env python3
"""Point the manifest's entrypoint at the functions list: set_includes.py <manifest> <toml name>"""
import re
import sys

path, name = sys.argv[1], sys.argv[2]
s = open(path).read()
s = re.sub(r'includes = \[\]', f'includes = ["{name}"]', s, count=1)
open(path, 'w').write(s)
