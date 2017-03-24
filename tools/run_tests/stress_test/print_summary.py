#!/usr/bin/env python
# Copyright 2016, Google Inc.
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
