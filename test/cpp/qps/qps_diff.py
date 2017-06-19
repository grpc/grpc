#!/usr/bin/env python2.7
#
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
""" Computes the diff between two qps runs and outputs significant results """

import shutil
import subprocess
import os
import multiprocessing
import json
import sys
import tabulate
import argparse

sys.path.append(
  os.path.join(
    os.path.dirname(sys.argv[0]), '..', '..', '..', 'tools', 'profiling', 'microbenchmarks', 'bm_diff'))
import bm_speedup

sys.path.append(
  os.path.join(
    os.path.dirname(sys.argv[0]), '..', '..', '..', 'tools', 'run_tests', 'python_utils'))
import comment_on_pr

_SCENARIOS = {
  'large-message-throughput': '{"scenarios":[{"name":"large-message-throughput", "spawn_local_worker_count": -2, "warmup_seconds": 30, "benchmark_seconds": 270, "num_servers": 1, "server_config": {"async_server_threads": 1, "security_params": null, "server_type": "ASYNC_SERVER"}, "num_clients": 1, "client_config": {"client_type": "ASYNC_CLIENT", "security_params": null, "payload_config": {"simple_params": {"resp_size": 1048576, "req_size": 1048576}}, "client_channels": 1, "async_client_threads": 1, "outstanding_rpcs_per_channel": 1, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"max_possible": 60000000000.0, "resolution": 0.01}}}]}',
  'multi-channel-64-KiB': '{"scenarios":[{"name":"multi-channel-64-KiB", "spawn_local_worker_count": -3, "warmup_seconds": 30, "benchmark_seconds": 270, "num_servers": 1, "server_config": {"async_server_threads": 31, "security_params": null, "server_type": "ASYNC_SERVER"}, "num_clients": 2, "client_config": {"client_type": "ASYNC_CLIENT", "security_params": null, "payload_config": {"simple_params": {"resp_size": 65536, "req_size": 65536}}, "client_channels": 32, "async_client_threads": 31, "outstanding_rpcs_per_channel": 100, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"max_possible": 60000000000.0, "resolution": 0.01}}}]}'
}

def _args():
  argp = argparse.ArgumentParser(
    description='Perform diff on QPS Driver')
  argp.add_argument(
    '-d',
    '--diff_base',
    type=str,
    help='Commit or branch to compare the current one to')
  argp.add_argument(
    '-l',
    '--loops',
    type=int,
    default=4,
    help='Number of times to loops the benchmarks. More loops cuts down on noise'
  )
  argp.add_argument(
    '-j',
    '--jobs',
    type=int,
    default=multiprocessing.cpu_count(),
    help='Number of CPUs to use')
  args = argp.parse_args()
  assert args.diff_base, "diff_base must be set"
  return args

def _make_cmd(jobs):
  return ['make', '-j', '%d' % jobs, 'qps_json_driver', 'qps_worker',]

def build(name, jobs):
  shutil.rmtree('qps_diff_%s' % name, ignore_errors=True)
  subprocess.check_call(['git', 'submodule', 'update'])
  try:
    subprocess.check_call(_make_cmd(jobs))
  except subprocess.CalledProcessError, e:
    subprocess.check_call(['make', 'clean'])
    subprocess.check_call(_make_cmd(jobs))
  os.rename('bins', 'qps_diff_%s' % name)

def _run_cmd(name, scenario, fname):
  return ['qps_diff_%s/opt/qps_json_driver' % name, '--scenarios_json', scenario, '--json_file_out', fname]

def run(name, scenarios, loops):
  for sn in scenarios:
    for i in range(0, loops):
      fname = "%s.%s.%d.json" % (sn, name, i)
      subprocess.check_call(_run_cmd(name, scenarios[sn], fname))

def _load_qps(fname):
  try:
    with open(fname) as f:
      return json.loads(f.read())['qps']
  except IOError, e:
    print("IOError occurred reading file: %s" % fname)
    return None
  except ValueError, e:
    print("ValueError occurred reading file: %s" % fname)
    return None

def _median(ary):
  assert (len(ary))
  ary = sorted(ary)
  n = len(ary)
  if n % 2 == 0:
    return (ary[(n - 1) / 2] + ary[(n - 1) / 2 + 1]) / 2.0
  else:
    return ary[n / 2]

def diff(scenarios, loops, old, new):
  old_data = {}
  new_data = {}

  # collect data
  for sn in scenarios:
    old_data[sn] = []
    new_data[sn] = []
    for i in range(0, loops):
      old_data[sn].append(_load_qps("%s.%s.%d.json" % (sn, old, i)))
      new_data[sn].append(_load_qps("%s.%s.%d.json" % (sn, new, i)))

  # crunch data
  headers = ['Benchmark', 'qps']
  rows = []
  for sn in scenarios:
    mdn_diff = abs(_median(new_data[sn]) - _median(old_data[sn]))
    print('%s: %s=%r %s=%r mdn_diff=%r' % (sn, new, new_data[sn], old, old_data[sn], mdn_diff))
    s = bm_speedup.speedup(new_data[sn], old_data[sn], 10e-5)
    if abs(s) > 3 and mdn_diff > 0.5:
      rows.append([sn, '%+d%%' % s])

  if rows:
    return tabulate.tabulate(rows, headers=headers, floatfmt='+.2f')
  else:
    return None

def main(args):

  build('new', args.jobs)

  if args.diff_base:
    where_am_i = subprocess.check_output(
      ['git', 'rev-parse', '--abbrev-ref', 'HEAD']).strip()
    subprocess.check_call(['git', 'checkout', args.diff_base])
    try:
      build('old', args.jobs)
    finally:
      subprocess.check_call(['git', 'checkout', where_am_i])
      subprocess.check_call(['git', 'submodule', 'update'])

  run('new', _SCENARIOS, args.loops)
  run('old', _SCENARIOS, args.loops)

  diff_output = diff(_SCENARIOS, args.loops, 'old', 'new')

  if diff_output:
    text = '[qps] Performance differences noted:\n%s' % diff_output
  else:
    text = '[qps] No significant performance differences'
  print('%s' % text)
  comment_on_pr.comment_on_pr('```\n%s\n```' % text)

if __name__ == '__main__':
  args = _args()
  main(args);
