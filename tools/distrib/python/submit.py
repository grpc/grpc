#!/usr/bin/env python

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
args = parser.parse_args()

# Move to the root directory of Python GRPC.
pkgdir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      '../../../src/python/src')
# Remove previous distributions; they somehow confuse twine.
try:
  shutil.rmtree(os.path.join(pkgdir, 'dist/'))
except:
  pass

# Make the push.
cmd = ['python', 'setup.py', 'sdist']
subprocess.call(cmd)

cmd = ['twine', 'upload', '-r', args.repository]
if args.identity is not None:
  cmd.extend(['-i', args.identity])
if args.username is not None:
  cmd.extend(['-u', args.username])
if args.password is not None:
  cmd.extend(['-p', args.password])
cmd.append('dist/*')

subprocess.call(cmd, cwd=pkgdir)
