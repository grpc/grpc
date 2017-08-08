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

"""Run tests using docker images in Google Container Registry per matrix."""

from __future__ import print_function

import argparse
import atexit
import json
import multiprocessing
import os
import re
import subprocess
import sys
import uuid

# Langauage Runtime Matrix
import client_matrix

python_util_dir = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../run_tests/python_utils'))
sys.path.append(python_util_dir)
import dockerjob
import jobset
import report_utils

_LANGUAGES = client_matrix.LANG_RUNTIME_MATRIX.keys()
# All gRPC release tags, flattened, deduped and sorted.
_RELEASES = sorted(list(set(
    i for l in client_matrix.LANG_RELEASE_MATRIX.values() for i in l)))
_TEST_TIMEOUT = 30

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
argp.add_argument('--gcr_path',
                  default='gcr.io/grpc-testing',
                  help='Path of docker images in Google Container Registry')

argp.add_argument('--release',
                  default='master',
                  choices=['all', 'master'] + _RELEASES,
                  help='Release tags to test.  When testing all '
                  'releases defined in client_matrix.py, use "all".')

argp.add_argument('-l', '--language',
                  choices=['all'] + sorted(_LANGUAGES),
                  nargs='+',
                  default=['all'],
                  help='Languages to test')

argp.add_argument('--keep',
                  action='store_true',
                  help='keep the created local images after finishing the tests.')

argp.add_argument('--report_file',
                  default='report.xml',
                  help='The result file to create.')

args = argp.parse_args()


def find_all_images_for_lang(lang):
  """Find docker images for a language across releases and runtimes.

  Returns dictionary of list of (<tag>, <image-full-path>) keyed by runtime.
  """
  # Find all defined releases.
  if args.release == 'all':
    releases = ['master'] + client_matrix.LANG_RELEASE_MATRIX[lang]
  else:
    # Look for a particular release.
    if args.release not in ['master'] + client_matrix.LANG_RELEASE_MATRIX[lang]:
      jobset.message('SKIPPED',
                     '%s for %s is not defined' % (args.release, lang),
                     do_newline=True)
      return []
    releases = [args.release]

  # Images tuples keyed by runtime.
  images = {}
  for runtime in client_matrix.LANG_RUNTIME_MATRIX[lang]:
    image_path = '%s/grpc_interop_%s' % (args.gcr_path, runtime)
    output = subprocess.check_output(['gcloud', 'beta', 'container', 'images',
                                        'list-tags', '--format=json', image_path])
    docker_image_list = json.loads(output)
    # All images should have a single tag or no tag.
    tags = [i['tags'][0] for i in docker_image_list if i['tags']]
    jobset.message('START', 'Found images for %s: %s' % (image_path, tags),
                   do_newline=True)
    skipped = len(docker_image_list) - len(tags)
    jobset.message('START', 'Skipped images (no-tag/unknown-tag): %d' % skipped,
                   do_newline=True)
    # Filter tags based on the releases.
    images[runtime] = [(tag,'%s:%s' % (image_path,tag)) for tag in tags if
                       tag in releases]
  return images

# caches test cases (list of JobSpec) loaded from file.  Keyed by lang and runtime.
_loaded_testcases = {}
def find_test_cases(lang, release):
  """Returns the list of test cases from testcase files per lang/release."""
  file_tmpl = os.path.join(os.path.dirname(__file__), 'testcases/%s__%s')
  if not os.path.exists(file_tmpl % (lang, release)):
    release = 'master'
  testcases = file_tmpl % (lang, release)
  if lang in _loaded_testcases.keys() and release in _loaded_testcases[lang].keys():
    return _loaded_testcases[lang][release]

  job_spec_list=[]
  try:
    with open(testcases) as f:
      # Only line start with 'docker run' are test cases.
      for line in f.readlines():
        if line.startswith('docker run'):
          m = re.search('--test_case=(.*)"', line)
          shortname = m.group(1) if m else 'unknown_test'
          spec = jobset.JobSpec(cmdline=line,
                                shortname=shortname,
                                timeout_seconds=_TEST_TIMEOUT,
                                shell=True)
          job_spec_list.append(spec)
      jobset.message('START',
                     'Loaded %s tests from %s' % (len(job_spec_list), testcases),
                     do_newline=True)
  except IOError as err:
    jobset.message('FAILED', err, do_newline=True)
  if lang not in _loaded_testcases.keys():
    _loaded_testcases[lang] = {}
  _loaded_testcases[lang][release]=job_spec_list
  return job_spec_list

_xml_report_tree = report_utils.new_junit_xml_tree()
def run_tests_for_lang(lang, runtime, images):
  """Find and run all test cases for a language.

  images is a list of (<release-tag>, <image-full-path>) tuple.
  """
  for image_tuple in images:
    release, image = image_tuple
    jobset.message('START', 'Testing %s' % image, do_newline=True)
    # Download the docker image before running each test case.
    subprocess.check_call(['gcloud', 'docker', '--', 'pull', image])
    _docker_images_cleanup.append(image)
    job_spec_list = find_test_cases(lang,release)
    num_failures, resultset = jobset.run(job_spec_list,
                                         newline_on_success=True,
                                         add_env={'docker_image':image},
                                         maxjobs=args.jobs)
    if num_failures:
      jobset.message('FAILED', 'Some tests failed', do_newline=True)
    else:
      jobset.message('SUCCESS', 'All tests passed', do_newline=True)

    report_utils.append_junit_xml_results(
        _xml_report_tree,
        resultset,
        'grpc_interop_matrix',
        '%s__%s %s'%(lang,runtime,release),
        str(uuid.uuid4()))

_docker_images_cleanup = []
def cleanup():
  if not args.keep:
    for image in _docker_images_cleanup:
      dockerjob.remove_image(image, skip_nonexistent=True)

atexit.register(cleanup)

languages = args.language if args.language != ['all'] else _LANGUAGES
for lang in languages:
  docker_images = find_all_images_for_lang(lang)
  for runtime in sorted(docker_images.keys()):
    run_tests_for_lang(lang, runtime, docker_images[runtime])

report_utils.create_xml_report_file(_xml_report_tree, args.report_file)
