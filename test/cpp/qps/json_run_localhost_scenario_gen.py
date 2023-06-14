#!/usr/bin/env python3

# Copyright 2018 gRPC authors.
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

import os
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.append(script_dir)

import scenario_generator_helper as gen

gen.generate_scenarios_bzl(
    gen.generate_json_run_localhost_scenarios(),
    os.path.join(script_dir, "json_run_localhost_scenarios.bzl"),
    "JSON_RUN_LOCALHOST_SCENARIOS",
)
