#!/usr/bin/env python2.7
# Copyright 2017 gRPC authors.
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

"""Build and upload docker images to Google Container Registry per matrix."""

from __future__ import print_function

import argparse
import atexit
import multiprocessing
import os
import shutil
import subprocess
import sys
import tempfile

# Langauage Runtime Matrix
import client_matrix

python_util_dir = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../run_tests/python_utils'))
sys.path.append(python_util_dir)
import dockerjob
import jobset

_IMAGE_BUILDER = 'tools/run_tests/dockerize/build_interop_image.sh'
_LANGUAGES = client_matrix.LANG_RUNTIME_MATRIX.keys()
# All gRPC release tags, flattened, deduped and sorted.
_RELEASES = sorted(list(set(
    i for l in client_matrix.LANG_RELEASE_MATRIX.values() for i in l)))

# Destination directory inside docker image to keep extra info from build time.
_BUILD_INFO = '/var/local/build_info'

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('--gcr_path',
                  default='gcr.io/grpc-testing',
                  help='Path of docker images in Google Container Registry')

argp.add_argument('--release',
                  default='master',
                  choices=['all', 'master'] + _RELEASES,
                  help='github commit tag to checkout.  When building all '
                  'releases defined in client_matrix.py, use "all". Valid only '
                  'with --git_checkout.')

argp.add_argument('-l', '--language',
                  choices=['all'] + sorted(_LANGUAGES),
                  nargs='+',
                  default=['all'],
                  help='Test languages to build docker images for.')

argp.add_argument('--git_checkout',
                  action='store_true',
                  help='Use a separate git clone tree for building grpc stack. '
                  'Required when using --release flag.  By default, current'
                  'tree and the sibling will be used for building grpc stack.')

argp.add_argument('--git_checkout_root',
                  default='/export/hda3/tmp/grpc_matrix',
                  help='Directory under which grpc-go/java/main repo will be '
                  'cloned.  Valid only with --git_checkout.')

argp.add_argument('--keep',
                  action='store_true',
                  help='keep the created local images after uploading to GCR')


args = argp.parse_args()

def add_files_to_image(image, with_files, label=None):
  """Add files to a docker image.

  image: docker image name, i.e. grpc_interop_java:26328ad8
  with_files: additional files to include in the docker image.
  label: label string to attach to the image.
  """
  tag_idx = image.find(':')
  if tag_idx == -1:
    jobset.message('FAILED', 'invalid docker image %s' % image, do_newline=True)
    sys.exit(1)
  orig_tag = '%s_' % image
  subprocess.check_output(['docker', 'tag', image, orig_tag])

  lines = ['FROM ' + orig_tag]
  if label:
    lines.append('LABEL %s' % label)

  temp_dir = tempfile.mkdtemp()
  atexit.register(lambda: subprocess.call(['rm', '-rf', temp_dir]))

  # Copy with_files inside the tmp directory, which will be the docker build
  # context.
  for f in with_files:
    shutil.copy(f, temp_dir)
    lines.append('COPY %s %s/' % (os.path.basename(f), _BUILD_INFO))

  # Create a Dockerfile.
  with open(os.path.join(temp_dir, 'Dockerfile'), 'w') as f:
    f.write('\n'.join(lines))

  jobset.message('START', 'Repackaging %s' % image, do_newline=True)
  build_cmd = ['docker', 'build', '--rm', '--tag', image, temp_dir]
  subprocess.check_output(build_cmd)
  dockerjob.remove_image(orig_tag, skip_nonexistent=True)

def build_image_jobspec(runtime, env, gcr_tag):
  """Build interop docker image for a language with runtime.

  runtime: a <lang><version> string, for example go1.8.
  env:     dictionary of env to passed to the build script.
  gcr_tag: the tag for the docker image (i.e. v1.3.0).
  """
  basename = 'grpc_interop_%s' % runtime
  tag = '%s/%s:%s' % (args.gcr_path, basename, gcr_tag)
  build_env = {
      'INTEROP_IMAGE': tag,
      'BASE_NAME': basename,
      'TTY_FLAG': '-t'
  }
  build_env.update(env)
  build_job = jobset.JobSpec(
          cmdline=[_IMAGE_BUILDER],
          environ=build_env,
          shortname='build_docker_%s' % runtime,
          timeout_seconds=30*60)
  build_job.tag = tag
  return build_job

def build_all_images_for_lang(lang):
  """Build all docker images for a language across releases and runtimes."""
  if not args.git_checkout:
    if args.release != 'master':
      print('WARNING: --release is set but will be ignored\n')
    releases = ['master']
  else:
    if args.release == 'all':
      releases = client_matrix.LANG_RELEASE_MATRIX[lang]
    else:
      # Build a particular release.
      if args.release not in ['master'] + client_matrix.LANG_RELEASE_MATRIX[lang]:
        jobset.message('SKIPPED',
                       '%s for %s is not defined' % (args.release, lang),
                       do_newline=True)
        return []
      releases = [args.release]

  images = []
  for release in releases:
    images += build_all_images_for_release(lang, release)
  jobset.message('SUCCESS',
                 'All docker images built for %s at %s.' % (lang, releases),
                 do_newline=True)
  return images

def build_all_images_for_release(lang, release):
  """Build all docker images for a release across all runtimes."""
  docker_images = []
  build_jobs = []

  env = {}
  # If we not using current tree or the sibling for grpc stack, do checkout.
  if args.git_checkout:
    stack_base = checkout_grpc_stack(lang, release)
    var ={'go': 'GRPC_GO_ROOT', 'java': 'GRPC_JAVA_ROOT'}.get(lang, 'GRPC_ROOT')
    env[var] = stack_base

  for runtime in client_matrix.LANG_RUNTIME_MATRIX[lang]:
    job = build_image_jobspec(runtime, env, release)
    docker_images.append(job.tag)
    build_jobs.append(job)

  jobset.message('START', 'Building interop docker images.', do_newline=True)
  print('Jobs to run: \n%s\n' % '\n'.join(str(j) for j in build_jobs))

  num_failures, _ = jobset.run(
      build_jobs, newline_on_success=True, maxjobs=multiprocessing.cpu_count())
  if num_failures:
    jobset.message('FAILED', 'Failed to build interop docker images.',
                   do_newline=True)
    docker_images_cleanup.extend(docker_images)
    sys.exit(1)

  jobset.message('SUCCESS',
                 'All docker images built for %s at %s.' % (lang, release),
                 do_newline=True)

  if release != 'master':
    commit_log = os.path.join(stack_base, 'commit_log')
    if os.path.exists(commit_log):
      for image in docker_images:
        add_files_to_image(image, [commit_log], 'release=%s' % release)
  return docker_images

def cleanup():
  if not args.keep:
    for image in docker_images_cleanup:
      dockerjob.remove_image(image, skip_nonexistent=True)

docker_images_cleanup = []
atexit.register(cleanup)

def checkout_grpc_stack(lang, release):
  """Invokes 'git check' for the lang/release and returns directory created."""
  assert args.git_checkout and args.git_checkout_root

  if not os.path.exists(args.git_checkout_root):
    os.makedirs(args.git_checkout_root)

  repo = client_matrix.get_github_repo(lang)
  # Get the subdir name part of repo
  # For example, 'git@github.com:grpc/grpc-go.git' should use 'grpc-go'.
  repo_dir = os.path.splitext(os.path.basename(repo))[0]
  stack_base = os.path.join(args.git_checkout_root, repo_dir)

  # Assume the directory is reusable for git checkout.
  if not os.path.exists(stack_base):
    subprocess.check_call(['git', 'clone', '--recursive', repo],
                          cwd=os.path.dirname(stack_base))

  # git checkout.
  jobset.message('START', 'git checkout %s from %s' % (release, stack_base),
                 do_newline=True)
  # We should NEVER do checkout on current tree !!!
  assert not os.path.dirname(__file__).startswith(stack_base)
  output = subprocess.check_output(
      ['git', 'checkout', release], cwd=stack_base, stderr=subprocess.STDOUT)
  commit_log = subprocess.check_output(['git', 'log', '-1'], cwd=stack_base)
  jobset.message('SUCCESS', 'git checkout', output + commit_log, do_newline=True)

  # Write git log to commit_log so it can be packaged with the docker image.
  with open(os.path.join(stack_base, 'commit_log'), 'w') as f:
    f.write(commit_log)
  return stack_base

languages = args.language if args.language != ['all'] else _LANGUAGES
for lang in languages:
  docker_images = build_all_images_for_lang(lang)
  for image in docker_images:
    jobset.message('START', 'Uploading %s' % image, do_newline=True)
    # docker image name must be in the format <gcr_path>/<image>:<gcr_tag>
    assert image.startswith(args.gcr_path) and image.find(':') != -1

    # subprocess.call(['gcloud', 'docker', '--', 'push', image])
    subprocess.call(['gcloud', 'docker', '--', 'push', image])
