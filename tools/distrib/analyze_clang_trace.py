#!/usr/bin/env python3
# Copyright 2026 gRPC authors.
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
import collections
import json
import os
import subprocess
import sys
import time
from pathlib import Path

def run_command(cmd, cwd):
    print(f"Running: {' '.join(cmd)}")
    subprocess.run(cmd, cwd=cwd, check=True)

def find_trace_files(root_dir):
    trace_files = []
    for path in Path(root_dir).rglob('*.json'):
        if not path.name.endswith('.compile_commands.json'):
            trace_files.append(path)
    return trace_files

def strip_template_params(name):
    angle_count = 0
    result = []
    in_params = False
    
    for char in name:
        if char == '<':
            if not in_params:
                result.append('<')
                in_params = True
            angle_count += 1
        elif char == '>':
            angle_count -= 1
            if angle_count == 0:
                result.append('>')
                in_params = False
        elif not in_params:
            result.append(char)
            
    return "".join(result)

def analyze_traces(trace_files):
    template_times = collections.defaultdict(int)
    
    for path in trace_files:
        try:
            with open(path, 'r', encoding='utf-8') as f:
                data = json.load(f)
                
            for event in data.get('traceEvents', []):
                if event.get('ph') == 'X' and event.get('name', '').startswith('Instantiate'):
                    detail = event.get('args', {}).get('detail', None)
                    dur = event.get('dur', 0)
                    if detail and dur > 0:
                        clean_name = strip_template_params(detail)
                        template_times[clean_name] += dur
        except Exception as e:
            print(f"Warning: Failed to process {path}: {e}", file=sys.stderr)
            
    return template_times

def main():
    parser = argparse.ArgumentParser(description="Run Bazel build with Clang time-trace and analyze template instantiation times.")
    parser.add_argument("target", help="Bazel target to build, e.g., //test/core/util:sorted_pack_test")
    parser.add_argument("--top-n", type=int, default=50, help="Number of top templates to display in the report (default 50)")
    parser.add_argument("--skip-build", action="store_true", help="Skip the bazel build phase and analyze existing traces directly")
    args = parser.parse_args()

    workspace_root = Path(__file__).resolve().parents[2]
    
    unique_timestamp = int(time.time())
    
    bazel_cmd = [
        "bazel", "build",
        "--copt=-ftime-trace",
        "--spawn_strategy=local",
        f"--copt=-D_FORCE_PROFILE_TIMESTAMP={unique_timestamp}",
        args.target
    ]

    if not args.skip_build:
        print(f"--- 1. Building target {args.target} with profiling enabled ---")
        try:
            run_command(bazel_cmd, cwd=workspace_root)
        except subprocess.CalledProcessError:
            print("Error: Build failed.", file=sys.stderr)
            sys.exit(1)
    else:
        print("--- 1. Skipping build phase as requested ---")

    print("--- 2. Searching for generated trace files ---")
    bazel_out = workspace_root / "bazel-out"
    trace_files = find_trace_files(bazel_out)
    
    if not trace_files:
        print("No trace files found! Ensure compilation actually occurred.", file=sys.stderr)
        sys.exit(1)
        
    print(f"Found {len(trace_files)} trace files.")

    print("--- 3. Analyzing template instantiations ---")
    template_times = analyze_traces(trace_files)
    
    if not template_times:
        print("No template instantiation events found in traces.")
        sys.exit(0)

    sorted_templates = sorted(template_times.items(), key=lambda item: item[1], reverse=True)

    print(f"\n--- Top {args.top_n} Templates by Total Instantiation Time ---")
    print(f"{'Total Time (ms)':>15} | {'Template Name'}")
    print("-" * 100)
    
    total_us_across_all = sum(template_times.values())
    
    for detail, dur_us in sorted_templates[:args.top_n]:
        dur_ms = dur_us / 1000.0
        print(f"{dur_ms:15.2f} | {detail}")

    print("-" * 100)
    print(f"Total instantiation time measured: {total_us_across_all / 1000.0:,.2f} ms")

if __name__ == "__main__":
    main()
