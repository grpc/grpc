#!/usr/bin/env python2.7
# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import os
import shutil
import subprocess

parser = argparse.ArgumentParser(
    description='Submit the package to a PyPI repository.')
parser.add_argument('--repository',
                    '-r',
                    metavar='r',
                    type=str,
                    default='pypi',
                    help='The repository to push the package to. '
                    'Ensure the value appears in your .pypirc file. '
                    'Defaults to "pypi".')
parser.add_argument('--identity',
                    '-i',
                    metavar='i',
                    type=str,
                    help='GPG identity to sign the files with.')
parser.add_argument(
    '--username',
    '-u',
    metavar='u',
    type=str,
    help='Username to authenticate with the repository. Not needed if you have '
    'configured your .pypirc to include your username.')
parser.add_argument(
    '--password',
    '-p',
    metavar='p',
    type=str,
    help='Password to authenticate with the repository. Not needed if you have '
    'configured your .pypirc to include your password.')
parser.add_argument(
    '--bdist',
    '-b',
    action='store_true',
    help='Generate a binary distribution (wheel) for the current OS.')
parser.add_argument(
    '--dist-args',
    type=str,
    help='Additional arguments to pass to the *dist setup.py command.')
args = parser.parse_args()

# Move to the root directory of Python GRPC.
pkgdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../')
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
