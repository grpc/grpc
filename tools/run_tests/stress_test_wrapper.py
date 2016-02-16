#!/usr/bin/env python2.7
import os
import re
import select
import subprocess
import sys
import time

GRPC_ROOT = '/usr/local/google/home/sreek/workspace/grpc/'
STRESS_TEST_IMAGE = GRPC_ROOT + 'bins/opt/stress_test'
STRESS_TEST_ARGS_STR = ' '.join([
    '--server_addresses=localhost:8000',
    '--test_cases=empty_unary:1,large_unary:1', '--num_stubs_per_channel=10',
    '--test_duration_secs=10'])
METRICS_CLIENT_IMAGE = GRPC_ROOT + 'bins/opt/metrics_client'
METRICS_CLIENT_ARGS_STR = ' '.join([
    '--metrics_server_address=localhost:8081', '--total_only=true'])
LOGFILE_NAME = 'stress_test.log'


# TODO (sree): Write a python grpc client to directly query the metrics instead
# of calling metrics_client
def get_qps(metrics_cmd):
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

def main(argv):
  # TODO(sree) Create BigQuery Tables
  # (Summary table), (Metrics table)

  # TODO(sree) Update status that the test is starting (in the status table)
  #

  metrics_cmd = [METRICS_CLIENT_IMAGE
                ] + [x for x in METRICS_CLIENT_ARGS_STR.split()]

  stress_cmd = [STRESS_TEST_IMAGE] + [x for x in STRESS_TEST_ARGS_STR.split()]
  # TODO(sree): Add an option to print to stdout if logfilename is absent
  logfile = open(LOGFILE_NAME, 'w')
  stress_p = subprocess.Popen(args=arg_list,
                              stdout=logfile,
                              stderr=subprocess.STDOUT)

  qps_history = [1, 1, 1]  # Maintain the last 3 qps
  qps_history_idx = 0  # Index into the qps_history list

  is_error = False
  while True:
    # Check if stress_client is still running. If so, collect metrics and upload
    # to BigQuery status table
    #
    if stress_p is not None:
      # TODO(sree) Upload completion status to BiqQuery
      is_error = (stress_p.returncode != 0)
      break

    # Stress client still running. Get metrics
    qps = get_qps(metrics_cmd)

    # If QPS has been zero for the last 3 iterations, flag it as error and exit
    qps_history[qps_history_idx] = qps
    qps_history_idx = (qps_histor_idx + 1) % len(qps_history)
    if sum(a) == 0:
      print ('QPS has been zero for the last 3 iterations. Not monitoring '
             'anymore. The stress test client may be stalled.')
      is_error = True
      break

    #TODO(sree) Upload qps metrics to BiqQuery

  if is_error:
    print 'Waiting indefinitely..'
    select.select([],[],[])

  return 1


if __name__ == '__main__':
  main(sys.argv[1:])
