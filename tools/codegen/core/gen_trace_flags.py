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

# Uses trace_flags.yaml to auto-generate code for trace flags in gRPC-core, as
# well as the trace flag piece of doc/environment_variables.md.

from io import StringIO
import os
import subprocess

from absl import app
from absl import flags
from mako.lookup import TemplateLookup
from mako.runtime import Context
from mako.template import Template
import yaml

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
    ctx = Context(buf, trace_flags=trace_flags, absl_prefix="")
    header_template.render_context(ctx)
    return buf.getvalue()


def main(args):
    with open("src/core/lib/debug/trace_flags.yaml") as f:
        trace_flags = yaml.safe_load(f.read())
    with open("src/core/lib/debug/trace_flags.h", "w") as f:
        f.write(render_source_file("trace_flags.h.mako", trace_flags))
    with open("src/core/lib/debug/trace_flags.cc", "w") as f:
        f.write(render_source_file("trace_flags.cc.mako", trace_flags))
    with open("doc/trace_flags.md", "w") as f:
        f.write(render_source_file("trace_flags.md.mako", trace_flags))
    if _CHECK.value or _FORMAT.value:
        env = os.environ.copy()
        env["CHANGED_FILES"] = "src/core/lib/debug/*"
        env["TEST"] = ""
        format_result = subprocess.run(
            ["tools/distrib/clang_format_code.sh"], env=env, capture_output=True
        )
        if format_result.returncode != 0:
            raise app.Error("Format failed")
    if _CHECK.value:
        diff_result = subprocess.run(["git", "diff"], capture_output=True)
        if len(diff_result.stdout) > 0 or len(diff_result.stderr) > 0:
            print(
                "Trace flags need to be generated. Please run tools/codegen/core/gen_trace_flags.py"
            )
            print(diff_result.stdout.decode("utf-8"))
            raise app.Error("diff found")


if __name__ == "__main__":
    app.run(main)
