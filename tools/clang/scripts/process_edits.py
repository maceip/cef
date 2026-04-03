#!/usr/bin/env python3
# Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
# reserved. Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

"""
Deduplicate and format cef_cpp_rewriter edit output.

Usage:
  cef_cpp_rewriter ... | python3 process_edits.py | python3 apply_edits.py --base-dir out/ClangTool
  python3 process_edits.py raw_output1.txt raw_output2.txt > edits.txt
"""

import sys


def main():
    # Read as binary to handle null bytes correctly
    if len(sys.argv) > 1:
        data = b''.join(open(f, 'rb').read() for f in sys.argv[1:])
    else:
        data = sys.stdin.buffer.read()

    lines = data.decode('utf-8', errors='replace').split('\n')

    seen = set()
    edits = []
    for line in lines:
        line = line.strip()
        if not line or not (line.startswith('r:::') or line.startswith('include-')):
            continue
        if line not in seen:
            seen.add(line)
            edits.append(line)

    print('==== BEGIN EDITS ====')
    for edit in sorted(edits):
        print(edit)
    print('==== END EDITS ====')


if __name__ == '__main__':
    main()
