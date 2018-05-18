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

parser = argparse.ArgumentParser()
parser.add_argument(
    '--config',
    metavar='c',
    type=str,
    nargs=1,
    help='GRPC/GPR libraries build configuration',
    default='opt')
parser.add_argument('--submit', action='store_true')
parser.add_argument('--gh-user', type=str, help='GitHub user to push as.')
parser.add_argument(
    '--gh-repo-owner',
    type=str,
    help=('Owner of the GitHub repository to be pushed; '
          'defaults to --gh-user.'))
parser.add_argument('--doc-branch', type=str)
args = parser.parse_args()

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..', '..'))

CONFIG = args.config
SETUP_PATH = os.path.join(PROJECT_ROOT, 'setup.py')
REQUIREMENTS_PATH = os.path.join(PROJECT_ROOT, 'requirements.txt')
DOC_PATH = os.path.join(PROJECT_ROOT, 'doc/build')
INCLUDE_PATH = os.path.join(PROJECT_ROOT, 'include')
LIBRARY_PATH = os.path.join(PROJECT_ROOT, 'libs/{}'.format(CONFIG))
VIRTUALENV_DIR = os.path.join(SCRIPT_DIR, 'distrib_virtualenv')
VIRTUALENV_PYTHON_PATH = os.path.join(VIRTUALENV_DIR, 'bin', 'python')
VIRTUALENV_PIP_PATH = os.path.join(VIRTUALENV_DIR, 'bin', 'pip')

environment = os.environ.copy()
environment.update({
    'CONFIG': CONFIG,
    'CFLAGS': '-I{}'.format(INCLUDE_PATH),
    'LDFLAGS': '-L{}'.format(LIBRARY_PATH),
    'LD_LIBRARY_PATH': LIBRARY_PATH,
    'GRPC_PYTHON_BUILD_WITH_CYTHON': '1',
    'GRPC_PYTHON_ENABLE_DOCUMENTATION_BUILD': '1',
})

subprocess_arguments_list = [
    {
        'args': ['virtualenv', VIRTUALENV_DIR],
        'env': environment
    },
    {
        'args': [VIRTUALENV_PIP_PATH, 'install', '--upgrade', 'pip==10.0.1'],
        'env': environment
    },
    {
        'args': [VIRTUALENV_PIP_PATH, 'install', '-r', REQUIREMENTS_PATH],
        'env': environment
    },
    {
        'args': [VIRTUALENV_PYTHON_PATH, SETUP_PATH, 'build'],
        'env': environment
    },
    {
        'args': [VIRTUALENV_PYTHON_PATH, SETUP_PATH, 'doc'],
        'env': environment
    },
]

for subprocess_arguments in subprocess_arguments_list:
    print('Running command: {}'.format(subprocess_arguments['args']))
    subprocess.check_call(**subprocess_arguments)

if args.submit:
    assert args.gh_user
    assert args.doc_branch
    github_user = args.gh_user
    github_repository_owner = (args.gh_repo_owner
                               if args.gh_repo_owner else args.gh_user)
    # Create a temporary directory out of tree, checkout gh-pages from the
    # specified repository, edit it, and push it. It's up to the user to then go
    # onto GitHub and make a PR against grpc/grpc:gh-pages.
    repo_parent_dir = tempfile.mkdtemp()
    print('Documentation parent directory: {}'.format(repo_parent_dir))
    repo_dir = os.path.join(repo_parent_dir, 'grpc')
    python_doc_dir = os.path.join(repo_dir, 'python')
    doc_branch = args.doc_branch

    print('Cloning your repository...')
    subprocess.check_call(
        [
            'git', 'clone', 'https://{}@github.com/{}/grpc'.format(
                github_user, github_repository_owner)
        ],
        cwd=repo_parent_dir)
    subprocess.check_call(
        ['git', 'remote', 'add', 'upstream', 'https://github.com/grpc/grpc'],
        cwd=repo_dir)
    subprocess.check_call(['git', 'fetch', 'upstream'], cwd=repo_dir)
    subprocess.check_call(
        ['git', 'checkout', 'upstream/gh-pages', '-b', doc_branch],
        cwd=repo_dir)
    print('Updating documentation...')
    shutil.rmtree(python_doc_dir, ignore_errors=True)
    shutil.copytree(DOC_PATH, python_doc_dir)
    print('Attempting to push documentation...')
    try:
        subprocess.check_call(['git', 'add', '--all'], cwd=repo_dir)
        subprocess.check_call(
            ['git', 'commit', '-m', 'Auto-update Python documentation'],
            cwd=repo_dir)
        subprocess.check_call(
            ['git', 'push', '--set-upstream', 'origin', doc_branch],
            cwd=repo_dir)
    except subprocess.CalledProcessError:
        print('Failed to push documentation. Examine this directory and push '
              'manually: {}'.format(repo_parent_dir))
        sys.exit(1)
    shutil.rmtree(repo_parent_dir)
