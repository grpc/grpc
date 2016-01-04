#!/usr/bin/env python2.7

# Copyright 2015-2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import datetime
import os
import re
import sys
import subprocess

# find our home
ROOT = os.path.abspath(
    os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

# parse command line
argp = argparse.ArgumentParser(description='copyright checker')
argp.add_argument('-o', '--output',
                  default='details',
                  choices=['list', 'details'])
argp.add_argument('-s', '--skips',
                  default=0,
                  action='store_const',
                  const=1)
argp.add_argument('-a', '--ancient',
                  default=0,
                  action='store_const',
                  const=1)
args = argp.parse_args()

# open the license text
with open('LICENSE') as f:
  LICENSE = f.read().splitlines()

# license format by file extension
# key is the file extension, value is a format string
# that given a line of license text, returns what should
# be in the file
LICENSE_PREFIX = {
  '.c':         r'\s*\*\s*',
  '.cc':        r'\s*\*\s*',
  '.h':         r'\s*\*\s*',
  '.m':         r'\s*\*\s*',
  '.php':       r'\s*\*\s*',
  '.js':        r'\s*\*\s*',
  '.py':        r'#\s*',
  '.rb':        r'#\s*',
  '.sh':        r'#\s*',
  '.proto':     r'//\s*',
  '.cs':        r'//\s*',
  '.mak':       r'#\s*',
  'Makefile':   r'#\s*',
  'Dockerfile': r'#\s*',
  'LICENSE':    '',
}

KNOWN_BAD = set([
  'src/php/tests/bootstrap.php',
])


RE_YEAR = r'Copyright (?:[0-9]+\-)?([0-9]+), Google Inc\.'
RE_LICENSE = dict(
    (k, r'\n'.join(
        LICENSE_PREFIX[k] +
        (RE_YEAR if re.search(RE_YEAR, line) else re.escape(line))
        for line in LICENSE))
     for k, v in LICENSE_PREFIX.iteritems())


def load(name):
  with open(name) as f:
    return '\n'.join(line.rstrip() for line in f.read().splitlines())


assert(re.search(RE_LICENSE['LICENSE'], load('LICENSE')))
assert(re.search(RE_LICENSE['Makefile'], load('Makefile')))


def log(cond, why, filename):
  if not cond: return
  if args.output == 'details':
    print '%s: %s' % (why, filename)
  else:
    print filename


# scan files, validate the text
for filename in subprocess.check_output('git ls-tree -r --name-only -r HEAD',
                                        shell=True).splitlines():
  if filename in KNOWN_BAD: continue
  ext = os.path.splitext(filename)[1]
  base = os.path.basename(filename)
  if ext in RE_LICENSE:
    re_license = RE_LICENSE[ext]
  elif base in RE_LICENSE:
    re_license = RE_LICENSE[base]
  else:
    log(args.skips, 'skip', filename)
    continue
  text = load(filename)
  ok = True
  m = re.search(re_license, text)
  if m:
    last_modified = int(subprocess.check_output('git log -1 --format="%ad" --date=short -- ' + filename, shell=True)[0:4])
    latest_claimed = int(m.group(1))
    if last_modified > latest_claimed:
      print '%s modified %d but copyright only extends to %d' % (filename, last_modified, latest_claimed)
      ok = False
  elif 'DO NOT EDIT' not in text and 'AssemblyInfo.cs' not in filename and filename != 'src/boringssl/err_data.c':
    log(1, 'copyright missing', filename)
    ok = False

sys.exit(0 if ok else 1)

