#!/usr/bin/env python3

# Copyright 2023 gRPC authors.
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
"""
Local QPS benchmark runner for the OSS Benchmark loadtest configurations.

This tool will run a scenario locally, either already extracted from
scenario_config_extractor, or extracted from a benchmark loadtest config. The
driver, client, and server all in the same process. You can run the process
under a custom runner using the --runner_cmd="<COMMAND>" flag, and with custom
environment variables if needed.

This example will run an optimized build of the loadtest under gdb

GRPC_VERBOSITY=debug \
    bazel run \
    --config=opt \
    --cxxopt="-gmlt" \
    test/cpp/qps:scenario_runner -- \
    --loadtest_file=/path/to/loadtest.config \
    --runner_cmd="gdb --args"

This builds the binary and runs:

    gdb --args bazel-bin/.../scenario_runner -- \
        --loadtest_config=/tmp/path/extracted_scenario_json.config

        
If you have already extracted the JSON scenario using scenario_config_exporter,
you can replace `--loadtest_file=loadtest.yaml` with
`--scenario_file=scenario.json`.


Other --runner_cmd examples:
    --runner_cmd="perf record -F 777 -o $(pwd)/perf.data -g --event=cpu-cycles",
    --runner_cmd="perf stat record -o $(pwd)/perf.stat.data",
"
"""


import os
import subprocess
import sys
import tempfile

from absl import app
from absl import flags
import yaml

_LOADTEST_YAML = flags.DEFINE_string(
    "loadtest_file", default=None, help="Path to the benchmark loadtest file"
)
_SCENARIO_JSON = flags.DEFINE_string(
    "scenario_file", default=None, help="Path to a scenario JSON file"
)
_RUNNER_CMD = flags.DEFINE_string(
    "runner_cmd",
    default="",
    help="Run the scearnio runner under a custom command (example: bazel ... --cmd='perf lock record -o $(pwd)/out')",
)
_RUN_FIRST = flags.DEFINE_bool(
    "run_first",
    default=False,
    help="Only run the first scenario in the loadtest",
)
_RUN_ALL = flags.DEFINE_bool(
    "run_all", default=False, help="Run all scenarios in the loadtest"
)


def run_command(filename):
    cmd = [
        os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "scenario_runner_cc",
        ),
        "--loadtest_config",
        filename,
    ]
    if _RUNNER_CMD.value:
        cmd = _RUNNER_CMD.value.split(" ") + cmd
    print(cmd)
    subprocess.run(cmd, check=True)
    if _RUN_FIRST.value:
        print("Exiting due to --run_first")
        sys.exit(0)


def run_loadtests():
    loadtests = []
    with open(
        os.path.join(
            os.path.dirname(os.path.abspath(__file__)), _LOADTEST_YAML.value
        )
    ) as f:
        loadtests = list(yaml.safe_load_all(f))
    if len(loadtests) > 1 and not (_RUN_FIRST.value or _RUN_ALL.value):
        print(
            "The loadtest configuration contains more than one scenario. Please specify --run_first or --run_all.",
            file=sys.stderr,
        )
        sys.exit(1)
    for loadtest in loadtests:
        with tempfile.NamedTemporaryFile() as tmp_f:
            tmp_f.write(
                "".join(loadtest["spec"]["scenariosJSON"]).encode("utf-8")
            )
            tmp_f.flush()
            run_command(tmp_f.name)


def run_scenario_file():
    run_command(_SCENARIO_JSON.value)


def main(args):
    if _LOADTEST_YAML.value:
        run_loadtests()
    elif _SCENARIO_JSON.value:
        run_scenario_file()
    else:
        "You must provide either a scenario.json or loadtest.yaml"


if __name__ == "__main__":
    app.run(main)
