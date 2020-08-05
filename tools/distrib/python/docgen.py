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

from __future__ import print_function

import argparse
import os
import os.path
import shutil
import subprocess
import sys
import tempfile
import grpc_version

parser = argparse.ArgumentParser()
parser.add_argument('--repo-owner',
                    type=str,
                    help=('Owner of the GitHub repository to be pushed'))
parser.add_argument('--doc-branch',
                    type=str,
                    default='python-doc-%s' % grpc_version.VERSION)
args = parser.parse_args()

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..', '..'))

SETUP_PATH = os.path.join(PROJECT_ROOT, 'setup.py')
REQUIREMENTS_PATH = os.path.join(PROJECT_ROOT, 'requirements.bazel.txt')
DOC_PATH = os.path.join(PROJECT_ROOT, 'doc/build')

if "VIRTUAL_ENV" in os.environ:
    VIRTUALENV_DIR = os.environ['VIRTUAL_ENV']
    PYTHON_PATH = os.path.join(VIRTUALENV_DIR, 'bin', 'python')
    subprocess_arguments_list = []
else:
    VIRTUALENV_DIR = os.path.join(SCRIPT_DIR, 'distrib_virtualenv')
    PYTHON_PATH = os.path.join(VIRTUALENV_DIR, 'bin', 'python')
    subprocess_arguments_list = [
        ['python3', '-m', 'virtualenv', VIRTUALENV_DIR],
    ]

subprocess_arguments_list += [
    [PYTHON_PATH, '-m', 'pip', 'install', '--upgrade', 'pip==19.3.1'],
    [PYTHON_PATH, '-m', 'pip', 'install', '-r', REQUIREMENTS_PATH],
    [PYTHON_PATH, '-m', 'pip', 'install', '--upgrade', 'Sphinx'],
    [PYTHON_PATH, SETUP_PATH, 'doc'],
]

for subprocess_arguments in subprocess_arguments_list:
    print('Running command: {}'.format(subprocess_arguments))
    subprocess.check_call(args=subprocess_arguments)

if not args.repo_owner or not args.doc_branch:
    tty_width = int(os.popen('stty size', 'r').read().split()[1])
    print('-' * tty_width)
    print('Please check generated Python doc inside doc/build')
    print(
        'To push to a GitHub repo, please provide repo owner and doc branch name'
    )
else:
    # Create a temporary directory out of tree, checkout gh-pages from the
    # specified repository, edit it, and push it. It's up to the user to then go
    # onto GitHub and make a PR against grpc/grpc:gh-pages.
    repo_parent_dir = tempfile.mkdtemp()
    print('Documentation parent directory: {}'.format(repo_parent_dir))
    repo_dir = os.path.join(repo_parent_dir, 'grpc')
    python_doc_dir = os.path.join(repo_dir, 'python')
    doc_branch = args.doc_branch

    print('Cloning your repository...')
    subprocess.check_call([
        'git',
        'clone',
        '--branch',
        'gh-pages',
        'https://github.com/grpc/grpc',
    ],
                          cwd=repo_parent_dir)
    subprocess.check_call(['git', 'checkout', '-b', doc_branch], cwd=repo_dir)
    subprocess.check_call([
        'git', 'remote', 'add', 'ssh-origin',
        'git@github.com:%s/grpc.git' % args.repo_owner
    ],
                          cwd=repo_dir)
    print('Updating documentation...')
    shutil.rmtree(python_doc_dir, ignore_errors=True)
    shutil.copytree(DOC_PATH, python_doc_dir)
    print('Attempting to push documentation to %s/%s...' %
          (args.repo_owner, doc_branch))
    try:
        subprocess.check_call(['git', 'add', '--all'], cwd=repo_dir)
        subprocess.check_call(
            ['git', 'commit', '-m', 'Auto-update Python documentation'],
            cwd=repo_dir)
        subprocess.check_call(
            ['git', 'push', '--set-upstream', 'ssh-origin', doc_branch],
            cwd=repo_dir)
    except subprocess.CalledProcessError:
        print('Failed to push documentation. Examine this directory and push '
              'manually: {}'.format(repo_parent_dir))
        sys.exit(1)
    shutil.rmtree(repo_parent_dir)
