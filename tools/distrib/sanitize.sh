#!/bin/bash
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

set -ex

cd $(dirname $0)/../..

tools/distrib/iwyu.sh || true
tools/buildgen/generate_projects.sh
tools/distrib/check_include_guards.py --fix
tools/distrib/check_naked_includes.py --fix || true
tools/distrib/check_copyright.py --fix
tools/distrib/add-iwyu.py
tools/distrib/check_trailing_newlines.sh --fix
tools/run_tests/sanity/check_port_platform.py --fix
tools/run_tests/sanity/check_include_style.py --fix || true
tools/distrib/check_namespace_qualification.py --fix
tools/distrib/black_code.sh
tools/distrib/isort_code.sh
tools/distrib/check_redundant_namespace_qualifiers.py || true
tools/codegen/core/gen_grpc_tls_credentials_options.py
tools/distrib/gen_experiments_and_format.sh

# Formatters should always run last
tools/distrib/clang_format_code.sh
tools/distrib/buildifier_format_code_strict.sh || true
