#!/usr/bin/env python3
# Copyright 2016 gRPC authors.
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

# Use to patch schema of existing scenario results tables (after adding fields).

from __future__ import print_function

import argparse
import calendar
import json
import os
import sys
import time
import uuid

gcp_utils_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../../gcp/utils")
)
sys.path.append(gcp_utils_dir)
import big_query_utils

_PROJECT_ID = "grpc-testing"


def _patch_results_table(dataset_id, table_id):
    bq = big_query_utils.create_big_query()
    with open(
        os.path.dirname(__file__) + "/scenario_result_schema.json", "r"
    ) as f:
        table_schema = json.loads(f.read())
    desc = "Results of performance benchmarks."
    return big_query_utils.patch_table(
        bq, _PROJECT_ID, dataset_id, table_id, table_schema
    )


argp = argparse.ArgumentParser(
    description="Patch schema of scenario results table."
)
argp.add_argument(
    "--bq_result_table",
    required=True,
    default=None,
    type=str,
    help='Bigquery "dataset.table" to patch.',
)

args = argp.parse_args()

dataset_id, table_id = args.bq_result_table.split(".", 2)

_patch_results_table(dataset_id, table_id)
print("Successfully patched schema of %s.\n" % args.bq_result_table)
