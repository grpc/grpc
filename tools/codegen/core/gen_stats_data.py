#!/usr/bin/env python3

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

from __future__ import print_function

import sys

import gen_stats_data_utils as utils
import yaml

with open("src/core/telemetry/stats_data.yaml") as f:
    attrs = yaml.safe_load(f.read())


def ProcessStatsDataDefs(stats_data_defs):
    """Process stats data definitions and return a list of attributes."""

    stats_names = set()
    stats_data_attrs = []

    for scoped_metrics in stats_data_defs:
        assert "scope" in scoped_metrics
        scope = scoped_metrics["scope"]
        linked_global_scope = scope
        is_scope_local = False
        if "global_scope" in scoped_metrics:
            is_scope_local = True
            linked_global_scope = scoped_metrics["global_scope"]
        for attr in scoped_metrics["metrics"]:
            if "counter" in attr:
                if attr["counter"] in stats_names:
                    print(
                        "ERROR: Stat name: %s redefined multiple times"
                        % attr["counter"]
                    )
                    sys.exit(1)
                else:
                    stats_names.add(attr["counter"])
                    attr["scope"] = scope
                    attr["linked_global_scope"] = linked_global_scope
                    stats_data_attrs.append(attr)
            elif "histogram" in attr:
                if attr["histogram"] in stats_names:
                    print(
                        "ERROR: Stat name: %s redefined multiple times"
                        % attr["histogram"]
                    )
                    sys.exit(1)
                else:
                    stats_names.add(attr["histogram"])
                    attr["scope"] = scope
                    attr["linked_global_scope"] = linked_global_scope
                    if is_scope_local:
                        assert "scope_buckets" in attr
                        assert "scope_counter_bits" in attr
                    stats_data_attrs.append(attr)
    return stats_data_attrs


stats_generator = utils.StatsDataGenerator(ProcessStatsDataDefs(attrs))
stats_generator.gen_stats_data_hdr("", "src/core/telemetry/stats_data.h")
stats_generator.gen_stats_data_src("src/core/telemetry/stats_data.cc")
