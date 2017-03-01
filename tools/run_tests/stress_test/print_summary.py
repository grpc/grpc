#!/usr/bin/env python2.7
# Copyright 2016, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import argparse
import os
import sys

stress_test_utils_dir = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../../gcp/stress_test'))
sys.path.append(stress_test_utils_dir)
from stress_test_utils import BigQueryHelper

argp = argparse.ArgumentParser(
    description='Print summary tables',
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)
argp.add_argument('--gcp_project_id',
                  required=True,
                  help='The Google Cloud Platform Project Id')
argp.add_argument('--dataset_id', type=str, required=True)
argp.add_argument('--run_id', type=str, required=True)
argp.add_argument('--summary_table_id', type=str, default='summary')
argp.add_argument('--qps_table_id', type=str, default='qps')
argp.add_argument('--summary_only', action='store_true', default=True)

if __name__ == '__main__':
  args = argp.parse_args()
  bq_helper = BigQueryHelper(args.run_id, '', '', args.gcp_project_id,
                             args.dataset_id, args.summary_table_id,
                             args.qps_table_id)
  bq_helper.initialize()
  if not args.summary_only:
    bq_helper.print_qps_records()
  bq_helper.print_summary_records()
