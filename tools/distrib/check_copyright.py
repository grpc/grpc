#!/usr/bin/env python2.7

# Copyright 2015, Google Inc.
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
import os
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
argp.add_argument('-f', '--fix',
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
LICENSE_FMT = {
  '.c': ' * %s',
  '.cc': ' * %s',
  '.h': ' * %s',
  '.m': ' * %s',
  '.php': ' * %s',
  '.py': '# %s',
  '.rb': '# %s',
  '.sh': '# %s',
  '.proto': '// %s',
  '.js': ' * %s',
  '.cs': '// %s',
  '.mak': '# %s',
  'Makefile': '# %s',
  'Dockerfile': '# %s',
}

# pregenerate the actual text that we should have
LICENSE_TEXT = dict(
    (k, '\n'.join((v % line).rstrip() for line in LICENSE))
    for k, v in LICENSE_FMT.iteritems())

OLD_LICENSE_TEXT = dict(
    (k, v.replace('2015', '2014')) for k, v in LICENSE_TEXT.iteritems())

def log(cond, why, filename):
  if not cond: return
  if args.output == 'details':
    print '%s: %s' % (why, filename)
  else:
    print filename

# scan files, validate the text
for filename in subprocess.check_output('git ls-tree -r --name-only -r HEAD',
                                        shell=True).splitlines():
  ext = os.path.splitext(filename)[1]
  base = os.path.basename(filename)
  if ext in LICENSE_TEXT: 
    license = LICENSE_TEXT[ext]
    old_license = OLD_LICENSE_TEXT[ext]
  elif base in LICENSE_TEXT:
    license = LICENSE_TEXT[base]
    old_license = OLD_LICENSE_TEXT[base]
  else:
    log(args.skips, 'skip', filename)
    continue
  with open(filename) as f:
    text = '\n'.join(line.rstrip() for line in f.read().splitlines())
  if license in text:
    pass
  elif old_license in text:
    log(args.ancient, 'old', filename)
    if args.fix:
      with open(filename, 'w') as f:
        f.write(text.replace('Copyright 2014, Google Inc.', 'Copyright 2015, Google Inc.') + '\n')
  elif 'DO NOT EDIT' not in text and 'AssemblyInfo.cs' not in filename:
    log(1, 'missing', filename)

