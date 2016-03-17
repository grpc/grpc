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
import re
import select
import subprocess
import sys
import time

from stress_test_utils import EventType
from stress_test_utils import BigQueryHelper


# TODO (sree): Write a python grpc client to directly query the metrics instead
# of calling metrics_client
def _get_qps(metrics_cmd):
  qps = 0
  try:
    # Note: gpr_log() writes even non-error messages to stderr stream. So it is 
    # important that we set stderr=subprocess.STDOUT
    p = subprocess.Popen(args=metrics_cmd,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
    retcode = p.wait()
    (out_str, err_str) = p.communicate()
    if retcode != 0:
      print 'Error in reading metrics information'
      print 'Output: ', out_str
    else:
      # The overall qps is printed at the end of the line
      m = re.search('\d+$', out_str)
      qps = int(m.group()) if m else 0
  except Exception as ex:
    print 'Exception while reading metrics information: ' + str(ex)
  return qps


def run_client():
  """This is a wrapper around the stress test client and performs the following:
      1) Create the following two tables in Big Query:
         (i) Summary table: To record events like the test started, completed
                            successfully or failed
        (ii) Qps table: To periodically record the QPS sent by this client
      2) Start the stress test client and add a row in the Big Query summary
         table
      3) Once every few seconds (as specificed by the poll_interval_secs) poll
         the status of the stress test client process and perform the
         following:
          3.1) If the process is still running, get the current qps by invoking
               the metrics client program and add a row in the Big Query
               Qps table. Sleep for a duration specified by poll_interval_secs
          3.2) If the process exited successfully, add a row in the Big Query
               Summary table and exit
          3.3) If the process failed, add a row in Big Query summary table and
               wait forever.
               NOTE: This script typically runs inside a GKE pod which means
               that the pod gets destroyed when the script exits. However, in
               case the stress test client fails, we would not want the pod to
               be destroyed (since we might want to connect to the pod for
               examining logs). This is the reason why the script waits forever
               in case of failures
  """
  env = dict(os.environ)
  image_type = env['STRESS_TEST_IMAGE_TYPE']
  image_name = env['STRESS_TEST_IMAGE']
  args_str = env['STRESS_TEST_ARGS_STR']
  metrics_client_image = env['METRICS_CLIENT_IMAGE']
  metrics_client_args_str = env['METRICS_CLIENT_ARGS_STR']
  run_id = env['RUN_ID']
  pod_name = env['POD_NAME']
  logfile_name = env.get('LOGFILE_NAME')
  poll_interval_secs = float(env['POLL_INTERVAL_SECS'])
  project_id = env['GCP_PROJECT_ID']
  dataset_id = env['DATASET_ID']
  summary_table_id = env['SUMMARY_TABLE_ID']
  qps_table_id = env['QPS_TABLE_ID']

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
    print 'Opening logfile: %s ...' % logfile_name
    details = 'Logfile: %s' % logfile_name
    logfile = open(logfile_name, 'w')

  # Update status that the test is starting (in the status table)
  bq_helper.insert_summary_row(EventType.STARTING, details)

  metrics_cmd = [metrics_client_image
                ] + [x for x in metrics_client_args_str.split()]
  stress_cmd = [image_name] + [x for x in args_str.split()]

  print 'Launching process %s ...' % stress_cmd
  stress_p = subprocess.Popen(args=stress_cmd,
                              stdout=logfile,
                              stderr=subprocess.STDOUT)

  qps_history = [1, 1, 1]  # Maintain the last 3 qps readings
  qps_history_idx = 0  # Index into the qps_history list

  is_error = False
  while True:
    # Check if stress_client is still running. If so, collect metrics and upload
    # to BigQuery status table
    if stress_p.poll() is not None:
      end_time = datetime.datetime.now().isoformat()
      event_type = EventType.SUCCESS
      details = 'End time: %s' % end_time
      if stress_p.returncode != 0:
        event_type = EventType.FAILURE
        details = 'Return code = %d. End time: %s' % (stress_p.returncode,
                                                      end_time)
        is_error = True
      bq_helper.insert_summary_row(event_type, details)
      print details
      break

    # Stress client still running. Get metrics
    qps = _get_qps(metrics_cmd)
    qps_recorded_at = datetime.datetime.now().isoformat()
    print 'qps: %d at %s' % (qps, qps_recorded_at)

    # If QPS has been zero for the last 3 iterations, flag it as error and exit
    qps_history[qps_history_idx] = qps
    qps_history_idx = (qps_history_idx + 1) % len(qps_history)
    if sum(qps_history) == 0:
      details = 'QPS has been zero for the last %d seconds - as of : %s' % (
          poll_interval_secs * 3, qps_recorded_at)
      is_error = True
      bq_helper.insert_summary_row(EventType.FAILURE, details)
      print details
      break

    # Upload qps metrics to BiqQuery
    bq_helper.insert_qps_row(qps, qps_recorded_at)

    time.sleep(poll_interval_secs)

  if is_error:
    print 'Waiting indefinitely..'
    select.select([], [], [])

  print 'Completed'
  return


if __name__ == '__main__':
  run_client()
