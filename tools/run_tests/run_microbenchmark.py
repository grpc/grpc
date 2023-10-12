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

import argparse
import html
import multiprocessing
import os
import subprocess
import sys

import python_utils.jobset as jobset
import python_utils.start_port_server as start_port_server

sys.path.append(
    os.path.join(
        os.path.dirname(sys.argv[0]),
        "..",
        "profiling",
        "microbenchmarks",
        "bm_diff",
    )
)
import bm_constants

flamegraph_dir = os.path.join(os.path.expanduser("~"), "FlameGraph")

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), "../.."))
if not os.path.exists("reports"):
    os.makedirs("reports")

start_port_server.start_port_server()


def fnize(s):
    out = ""
    for c in s:
        if c in "<>, /":
            if len(out) and out[-1] == "_":
                continue
            out += "_"
        else:
            out += c
    return out


# index html
index_html = """
<html>
<head>
<title>Microbenchmark Results</title>
</head>
<body>
"""


def heading(name):
    global index_html
    index_html += "<h1>%s</h1>\n" % name


def link(txt, tgt):
    global index_html
    index_html += '<p><a href="%s">%s</a></p>\n' % (
        html.escape(tgt, quote=True),
        html.escape(txt),
    )


def text(txt):
    global index_html
    index_html += "<p><pre>%s</pre></p>\n" % html.escape(txt)


def _bazel_build_benchmark(bm_name, cfg):
    """Build given benchmark with bazel"""
    subprocess.check_call(
        [
            "tools/bazel",
            "build",
            "--config=%s" % cfg,
            "//test/cpp/microbenchmarks:%s" % bm_name,
        ]
    )


def run_summary(bm_name, cfg, base_json_name):
    _bazel_build_benchmark(bm_name, cfg)
    cmd = [
        "bazel-bin/test/cpp/microbenchmarks/%s" % bm_name,
        "--benchmark_out=%s.%s.json" % (base_json_name, cfg),
        "--benchmark_out_format=json",
    ]
    if args.summary_time is not None:
        cmd += ["--benchmark_min_time=%d" % args.summary_time]
    return subprocess.check_output(cmd).decode("UTF-8")


def collect_summary(bm_name, args):
    # no counters, run microbenchmark and add summary
    # both to HTML report and to console.
    nocounters_heading = "Summary: %s" % bm_name
    nocounters_summary = run_summary(bm_name, "opt", bm_name)
    heading(nocounters_heading)
    text(nocounters_summary)
    print(nocounters_heading)
    print(nocounters_summary)


collectors = {
    "summary": collect_summary,
}

argp = argparse.ArgumentParser(description="Collect data from microbenchmarks")
argp.add_argument(
    "-c",
    "--collect",
    choices=sorted(collectors.keys()),
    nargs="*",
    default=sorted(collectors.keys()),
    help="Which collectors should be run against each benchmark",
)
argp.add_argument(
    "-b",
    "--benchmarks",
    choices=bm_constants._AVAILABLE_BENCHMARK_TESTS,
    default=bm_constants._AVAILABLE_BENCHMARK_TESTS,
    nargs="+",
    type=str,
    help="Which microbenchmarks should be run",
)
argp.add_argument(
    "--bq_result_table",
    default="",
    type=str,
    help=(
        "Upload results from summary collection to a specified bigquery table."
    ),
)
argp.add_argument(
    "--summary_time",
    default=None,
    type=int,
    help="Minimum time to run benchmarks for the summary collection",
)
args = argp.parse_args()

try:
    for collect in args.collect:
        for bm_name in args.benchmarks:
            collectors[collect](bm_name, args)
finally:
    if not os.path.exists("reports"):
        os.makedirs("reports")
    index_html += "</body>\n</html>\n"
    with open("reports/index.html", "w") as f:
        f.write(index_html)
