#!/usr/bin/env python3

# Copyright 2024 gRPC authors.
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

import subprocess
from io import StringIO

import yaml
from absl import app, flags
from mako.lookup import TemplateLookup
from mako.runtime import Context
from mako.template import Template

_CHECK = flags.DEFINE_bool(
    "check", default=False, help="Format and compare output using git diff."
)

_FORMAT = flags.DEFINE_bool(
    "format", default=False, help="Format the trace code after generating it."
)

tmpl_dir_ = TemplateLookup(directories=["tools/codegen/core/templates/"])


def render_source_file(tmpl_name, trace_flags):
    header_template = tmpl_dir_.get_template(tmpl_name)
    buf = StringIO()
    ctx = Context(buf, trace_flags=trace_flags)
    header_template.render_context(ctx)
    return buf.getvalue()


def main(args):
    with open("src/core/lib/debug/trace_flags.yaml") as f:
        trace_flags = yaml.safe_load(f.read())
    with open("src/core/lib/debug/trace.h", "w") as f:
        f.write(render_source_file("trace.h.mako", trace_flags))
    with open("src/core/lib/debug/trace_flags.cc", "w") as f:
        f.write(render_source_file("trace_flags.cc.mako", trace_flags))
    if _CHECK.value or _FORMAT.value:
        env = {
            "CHANGED_FILES": "src/core/lib/debug/trace.h src/core/lib/debug/trace_flags.cc"
        }
        subprocess.run(["tools/distrib/clang_format_code.sh"], check=True, env=env)
    if _CHECK.value:
        diff_result = subprocess.run(
            ["git", "diff", "src/core/lib/debug/"], check=True, capture_output=True
        )
        if len(diff_result.stdout) > 0 or len(diff_result.stderr) > 0:
            print(
                "Trace flags need to be generated. Please run tools/codegen/core/gen_trace_flags.py"
            )
            return 11


if __name__ == "__main__":
    app.run(main)
