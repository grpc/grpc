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
import shutil
import subprocess

parser = argparse.ArgumentParser(
    description='Submit the package to a PyPI repository.')
parser.add_argument(
    '--repository', '-r', metavar='r', type=str, default='pypi',
    help='The repository to push the package to. '
         'Ensure the value appears in your .pypirc file. '
         'Defaults to "pypi".'
)
parser.add_argument(
    '--identity', '-i', metavar='i', type=str,
    help='GPG identity to sign the files with.'
)
parser.add_argument(
    '--username', '-u', metavar='u', type=str,
    help='Username to authenticate with the repository. Not needed if you have '
         'configured your .pypirc to include your username.'
)
parser.add_argument(
    '--password', '-p', metavar='p', type=str,
    help='Password to authenticate with the repository. Not needed if you have '
         'configured your .pypirc to include your password.'
)
parser.add_argument(
    '--bdist', '-b', action='store_true',
    help='Generate a binary distribution (wheel) for the current OS.'
)
parser.add_argument(
    '--dist-args', type=str,
    help='Additional arguments to pass to the *dist setup.py command.'
)
args = parser.parse_args()

# Move to the root directory of Python GRPC.
pkgdir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      '../../../')
# Remove previous distributions; they somehow confuse twine.
try:
  shutil.rmtree(os.path.join(pkgdir, 'dist/'))
except:
  pass

# Build the Cython C files
build_env = os.environ.copy()
build_env['GRPC_PYTHON_BUILD_WITH_CYTHON'] = "1"
cmd = ['python', 'setup.py', 'build_ext', '--inplace']
subprocess.call(cmd, cwd=pkgdir, env=build_env)

# Make the push.
if args.bdist:
  cmd = ['python', 'setup.py', 'bdist_wheel']
else:
  cmd = ['python', 'setup.py', 'sdist']
if args.dist_args:
  cmd += args.dist_args.split()
subprocess.call(cmd, cwd=pkgdir)

cmd = ['twine', 'upload', '-r', args.repository]
if args.identity is not None:
  cmd.extend(['-i', args.identity])
if args.username is not None:
  cmd.extend(['-u', args.username])
if args.password is not None:
  cmd.extend(['-p', args.password])
cmd.append('dist/*')

subprocess.call(cmd, cwd=pkgdir)
