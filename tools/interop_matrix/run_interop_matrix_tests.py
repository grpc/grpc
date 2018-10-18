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

# Language Runtime Matrix
import client_matrix

python_util_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '../run_tests/python_utils'))
sys.path.append(python_util_dir)
import dockerjob
import jobset
import report_utils
import upload_test_results

_TEST_TIMEOUT_SECONDS = 60
_PULL_IMAGE_TIMEOUT_SECONDS = 10 * 60
_LANGUAGES = client_matrix.LANG_RUNTIME_MATRIX.keys()
# All gRPC release tags, flattened, deduped and sorted.
_RELEASES = sorted(
    list(
        set(
            client_matrix.get_release_tag_name(info)
            for lang in client_matrix.LANG_RELEASE_MATRIX.values()
            for info in lang)))

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
argp.add_argument(
    '--gcr_path',
    default='gcr.io/grpc-testing',
    help='Path of docker images in Google Container Registry')
argp.add_argument(
    '--release',
    default='all',
    choices=['all'] + _RELEASES,
    help='Release tags to test.  When testing all '
    'releases defined in client_matrix.py, use "all".')
argp.add_argument(
    '-l',
    '--language',
    choices=['all'] + sorted(_LANGUAGES),
    nargs='+',
    default=['all'],
    help='Languages to test')
argp.add_argument(
    '--keep',
    action='store_true',
    help='keep the created local images after finishing the tests.')
argp.add_argument(
    '--report_file', default='report.xml', help='The result file to create.')
argp.add_argument(
    '--allow_flakes',
    default=False,
    action='store_const',
    const=True,
    help=('Allow flaky tests to show as passing (re-runs failed '
          'tests up to five times)'))
argp.add_argument(
    '--bq_result_table',
    default='',
    type=str,
    nargs='?',
    help='Upload test results to a specified BQ table.')
argp.add_argument(
    '--server_host',
    default='74.125.206.210',
    type=str,
    nargs='?',
    help='The gateway to backend services.')


def _get_test_images_for_lang(lang, release_arg, image_path_prefix):
    """Find docker images for a language across releases and runtimes.

  Returns dictionary of list of (<tag>, <image-full-path>) keyed by runtime.
  """
    if release_arg == 'all':
        # Use all defined releases for given language
        releases = client_matrix.get_release_tags(lang)
    else:
        # Look for a particular release.
        if release_arg not in client_matrix.get_release_tags(lang):
            jobset.message(
                'SKIPPED',
                'release %s for %s is not defined' % (release_arg, lang),
                do_newline=True)
            return {}
        releases = [release_arg]

    # Images tuples keyed by runtime.
    images = {}
    for runtime in client_matrix.LANG_RUNTIME_MATRIX[lang]:
        image_path = '%s/grpc_interop_%s' % (image_path_prefix, runtime)
        images[runtime] = [
            (tag, '%s:%s' % (image_path, tag)) for tag in releases
        ]
    return images


def _read_test_cases_file(lang, runtime, release):
    """Read test cases from a bash-like file and return a list of commands"""
    testcase_dir = os.path.join(os.path.dirname(__file__), 'testcases')
    filename_prefix = lang
    if lang == 'csharp':
        # TODO(jtattermusch): remove this odd specialcase
        filename_prefix = runtime
    # Check to see if we need to use a particular version of test cases.
    lang_version = '%s_%s' % (filename_prefix, release)
    if lang_version in client_matrix.TESTCASES_VERSION_MATRIX:
        testcase_file = os.path.join(
            testcase_dir, client_matrix.TESTCASES_VERSION_MATRIX[lang_version])
    else:
        # TODO(jtattermusch): remove the double-underscore, it is pointless
        testcase_file = os.path.join(testcase_dir,
                                     '%s__master' % filename_prefix)

    lines = []
    with open(testcase_file) as f:
        for line in f.readlines():
            line = re.sub('\\#.*$', '', line)  # remove hash comments
            line = line.strip()
            if line and not line.startswith('echo'):
                # Each non-empty line is a treated as a test case command
                lines.append(line)
    return lines


def _cleanup_docker_image(image):
    jobset.message('START', 'Cleanup docker image %s' % image, do_newline=True)
    dockerjob.remove_image(image, skip_nonexistent=True)


args = argp.parse_args()


# caches test cases (list of JobSpec) loaded from file.  Keyed by lang and runtime.
def _generate_test_case_jobspecs(lang, runtime, release, suite_name):
    """Returns the list of test cases from testcase files per lang/release."""
    testcase_lines = _read_test_cases_file(lang, runtime, release)

    job_spec_list = []
    for line in testcase_lines:
        m = re.search('--test_case=(.*)"', line)
        shortname = m.group(1) if m else 'unknown_test'
        m = re.search('--server_host_override=(.*).sandbox.googleapis.com',
                      line)
        server = m.group(1) if m else 'unknown_server'

        # If server_host arg is not None, replace the original
        # server_host with the one provided or append to the end of
        # the command if server_host does not appear originally.
        if args.server_host:
            if line.find('--server_host=') > -1:
                line = re.sub('--server_host=[^ ]*',
                              '--server_host=%s' % args.server_host, line)
            else:
                line = '%s --server_host=%s"' % (line[:-1], args.server_host)

        spec = jobset.JobSpec(
            cmdline=line,
            shortname='%s:%s:%s:%s' % (suite_name, lang, server, shortname),
            timeout_seconds=_TEST_TIMEOUT_SECONDS,
            shell=True,
            flake_retries=5 if args.allow_flakes else 0)
        job_spec_list.append(spec)
    return job_spec_list


def _pull_images_for_lang(lang, images):
    """Pull all images for given lang from container registry."""
    jobset.message(
        'START', 'Downloading images for language "%s"' % lang, do_newline=True)
    download_specs = []
    for release, image in images:
        # Pull the image and warm it up.
        # First time we use an image with "docker run", it takes time to unpack the image
        # and later this delay would fail our test cases.
        cmdline = [
            'gcloud docker -- pull %s && docker run --rm=true %s /bin/true' %
            (image, image)
        ]
        spec = jobset.JobSpec(
            cmdline=cmdline,
            shortname='pull_image_%s' % (image),
            timeout_seconds=_PULL_IMAGE_TIMEOUT_SECONDS,
            shell=True)
        download_specs.append(spec)
    num_failures, resultset = jobset.run(
        download_specs, newline_on_success=True, maxjobs=args.jobs)
    if num_failures:
        jobset.message(
            'FAILED', 'Failed to download some images', do_newline=True)
        return False
    else:
        jobset.message(
            'SUCCESS', 'All images downloaded successfully.', do_newline=True)
        return True


def _run_tests_for_lang(lang, runtime, images, xml_report_tree):
    """Find and run all test cases for a language.

  images is a list of (<release-tag>, <image-full-path>) tuple.
  """
    # Fine to ignore return value as failure to download will result in test failure
    # later anyway.
    _pull_images_for_lang(lang, images)

    total_num_failures = 0
    for release, image in images:
        suite_name = '%s__%s_%s' % (lang, runtime, release)
        job_spec_list = _generate_test_case_jobspecs(lang, runtime, release,
                                                     suite_name)

        if not job_spec_list:
            jobset.message(
                'FAILED', 'No test cases were found.', do_newline=True)
            return 1

        num_failures, resultset = jobset.run(
            job_spec_list,
            newline_on_success=True,
            add_env={'docker_image': image},
            maxjobs=args.jobs)
        if args.bq_result_table and resultset:
            upload_test_results.upload_interop_results_to_bq(
                resultset, args.bq_result_table)
        if num_failures:
            jobset.message('FAILED', 'Some tests failed', do_newline=True)
            total_num_failures += num_failures
        else:
            jobset.message('SUCCESS', 'All tests passed', do_newline=True)

        report_utils.append_junit_xml_results(xml_report_tree, resultset,
                                              'grpc_interop_matrix', suite_name,
                                              str(uuid.uuid4()))

        if not args.keep:
            _cleanup_docker_image(image)

    return total_num_failures


languages = args.language if args.language != ['all'] else _LANGUAGES
total_num_failures = 0
_xml_report_tree = report_utils.new_junit_xml_tree()
for lang in languages:
    docker_images = _get_test_images_for_lang(lang, args.release, args.gcr_path)
    for runtime in sorted(docker_images.keys()):
        total_num_failures += _run_tests_for_lang(
            lang, runtime, docker_images[runtime], _xml_report_tree)

report_utils.create_xml_report_file(_xml_report_tree, args.report_file)

if total_num_failures:
    sys.exit(1)
sys.exit(0)
