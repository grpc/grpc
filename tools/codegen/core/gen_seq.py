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

import sys

from mako.lookup import TemplateLookup
from mako.template import Template

files = {
    "test/core/promise/opt_seq_test.cc": ("opt_seq_test.mako", {"n": 6}),
    "test/core/promise/opt_try_seq_test.cc": (
        "opt_try_seq_test.mako",
        {"n": 6},
    ),
    "src/core/lib/promise/detail/seq_state.h": (
        "seq_state.mako",
        {"max_steps": 14},
    ),
}

tmpl_lookup = TemplateLookup(directories=["tools/codegen/core/templates/"])
for filename, (template_name, args) in files.items():
    template = tmpl_lookup.get_template(template_name)
    with open(filename, "w") as f:
        print(template.render(**args), file=f)
