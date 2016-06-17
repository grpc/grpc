#!/usr/bin/env python2.7
# Copyright 2015-2016, Google Inc.
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

import datetime
import os
import resource
import select
import subprocess
import sys
import time

from stress_test_utils import BigQueryHelper
from stress_test_utils import EventType


def run_server():
  """This is a wrapper around the interop server and performs the following:
      1) Create a 'Summary table' in Big Query to record events like the server
         started, completed successfully or failed. NOTE: This also creates
         another table called the QPS table which is currently NOT needed on the
         server (it is needed on the stress test clients)
      2) Start the server process and add a row in Big Query summary table
      3) Wait for the server process to terminate. The server process does not
         terminate unless there is an error.
         If the server process terminated with a failure, add a row in Big Query
         and wait forever.
         NOTE: This script typically runs inside a GKE pod which means that the
         pod gets destroyed when the script exits. However, in case the server
         process fails, we would not want the pod to be destroyed (since we
         might want to connect to the pod for examining logs). This is the
         reason why the script waits forever in case of failures.
  """
  # Set the 'core file' size to 'unlimited' so that 'core' files are generated
  # if the server crashes (Note: This is not relevant for Java and Go servers)
  resource.setrlimit(resource.RLIMIT_CORE,
                     (resource.RLIM_INFINITY, resource.RLIM_INFINITY))

  # Read the parameters from environment variables
  env = dict(os.environ)

  run_id = env['RUN_ID']  # The unique run id for this test
  image_type = env['STRESS_TEST_IMAGE_TYPE']
  stress_server_cmd = env['STRESS_TEST_CMD'].split()
  args_str = env['STRESS_TEST_ARGS_STR']
  pod_name = env['POD_NAME']
  project_id = env['GCP_PROJECT_ID']
  dataset_id = env['DATASET_ID']
  summary_table_id = env['SUMMARY_TABLE_ID']
  qps_table_id = env['QPS_TABLE_ID']
  # The following parameter is to inform us whether the server runs forever
  # until forcefully stopped or will it naturally stop after sometime.
  # This way, we know that the process should not terminate (even if it does
  # with a success exit code) and flag any termination as a failure.
  will_run_forever = env.get('WILL_RUN_FOREVER', '1')

  logfile_name = env.get('LOGFILE_NAME')

  print('pod_name: %s, project_id: %s, run_id: %s, dataset_id: %s, '
        'summary_table_id: %s, qps_table_id: %s') % (pod_name, project_id,
                                                     run_id, dataset_id,
                                                     summary_table_id,
                                                     qps_table_id)

  bq_helper = BigQueryHelper(run_id, image_type, pod_name, project_id,
                             dataset_id, summary_table_id, qps_table_id)
  bq_helper.initialize()

  # Create BigQuery Dataset and Tables: Summary Table and Metrics Table
  if not bq_helper.setup_tables():
    print 'Error in creating BigQuery tables'
    return

  start_time = datetime.datetime.now()

  logfile = None
  details = 'Logging to stdout'
  if logfile_name is not None:
    print 'Opening log file: ', logfile_name
    logfile = open(logfile_name, 'w')
    details = 'Logfile: %s' % logfile_name

  stress_cmd = stress_server_cmd + [x for x in args_str.split()]

  details = '%s, Stress server command: %s' % (details, str(stress_cmd))
  # Update status that the test is starting (in the status table)
  bq_helper.insert_summary_row(EventType.STARTING, details)

  print 'Launching process %s ...' % stress_cmd
  stress_p = subprocess.Popen(args=stress_cmd,
                              stdout=logfile,
                              stderr=subprocess.STDOUT)

  # Update the status to running if subprocess.Popen launched the server
  if stress_p.poll() is None:
    bq_helper.insert_summary_row(EventType.RUNNING, '')

  # Wait for the server process to terminate
  returncode = stress_p.wait()

  if will_run_forever == '1' or returncode != 0:
    end_time = datetime.datetime.now().isoformat()
    event_type = EventType.FAILURE
    details = 'Returncode: %d; End time: %s' % (returncode, end_time)
    bq_helper.insert_summary_row(event_type, details)
    print 'Waiting indefinitely..'
    select.select([], [], [])
  return returncode


if __name__ == '__main__':
  run_server()
