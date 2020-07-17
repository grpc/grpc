#!/usr/bin/env python
# Copyright 2020 gRPC authors.
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
"""Run additional PHP tests"""

from __future__ import print_function

import os
import sys

import python_utils.jobset as jobset
import python_utils.report_utils as report_utils

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(_ROOT)

_DEFAULT_RUNTESTS_TIMEOUT = 1 * 60 * 60

# Number of jobs assigned to each job instance
_DEFAULT_NUM_JOBS = 2

# Name of the top-level umbrella report that includes all the test invocations
_REPORT_NAME = 'toplevel_php_extratests_invocations'


def _safe_report_name(name):
    """Reports with '+' in target name won't show correctly in ResultStore"""
    return name.replace('+', 'p')


def _report_filename(name):
    """Generates report file name with directory structure that leads to better presentation by internal CI"""
    # 'sponge_log.xml' suffix must be there for results to get recognized by kokoro.
    return '%s/%s' % (_safe_report_name(name), 'sponge_log.xml')


def _job_logfilename(shortname):
    """Generate location for log file that will match the sponge_log.xml from the top-level report."""
    # 'sponge_log.log' suffix must be there for log to get recognized as "target log"
    # for the corresponding 'sponge_log.xml' report.
    # the shortname_for_multi_target component must be set to match the sponge_log.xml location
    # because the top-level render_junit_xml_report is called with multi_target=True
    return '%s/%s/%s' % (_REPORT_NAME, shortname, 'sponge_log.log')


def _create_php_docker_job(docker_image_shortname):
    shortname = 'run_php_docker_test_%s' % docker_image_shortname
    return jobset.JobSpec(cmdline=[
        'src/php/bin/build_all_docker_images.sh', docker_image_shortname
    ],
                          shortname=shortname,
                          timeout_seconds=_DEFAULT_RUNTESTS_TIMEOUT,
                          logfilename=_job_logfilename(shortname))


if __name__ == "__main__":
    jobs = []
    jobs.append(_create_php_docker_job('grpc-ext'))
    jobs.append(_create_php_docker_job('php-zts'))

    print('Will run these tests:')
    for job in jobs:
        print('  %s: "%s"' % (job.shortname, ' '.join(job.cmdline)))
    print('')

    jobset.message('START', 'Running additional PHP tests', do_newline=True)
    num_failures, resultset = jobset.run(jobs,
                                         newline_on_success=True,
                                         travis=True,
                                         maxjobs=_DEFAULT_NUM_JOBS)

    report_utils.render_junit_xml_report(resultset,
                                         _report_filename(_REPORT_NAME),
                                         suite_name=_REPORT_NAME,
                                         multi_target=True)
    if num_failures == 0:
        jobset.message('SUCCESS',
                       'All PHP tests finished successfully.',
                       do_newline=True)
    else:
        jobset.message('FAILED', 'Some PHP tests have failed.', do_newline=True)
        sys.exit(1)
