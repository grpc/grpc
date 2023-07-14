# Copyright 2021 the gRPC authors.
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
Includes default copts.
"""

# This is a list of llvm flags to be used when being built with use_strict_warning=1
GRPC_LLVM_WARNING_FLAGS = [
    # Enable all & extra warnings
    "-Wall",
    "-Wextra",
    # Avoid some known traps
    "-Wimplicit-fallthrough",
    # Consider warnings as errors
    "-Werror",
    # Ignore unknown warning flags
    "-Wno-unknown-warning-option",
    # A list of enabled flags coming from internal build system
    "-Wc++20-extensions",
    "-Wctad-maybe-unsupported",
    "-Wdeprecated-increment-bool",
    "-Wfloat-overflow-conversion",
    "-Wfloat-zero-conversion",
    "-Wfor-loop-analysis",
    "-Wformat-security",
    "-Wgnu-redeclared-enum",
    "-Winfinite-recursion",
    "-Wliteral-conversion",
    "-Wnon-virtual-dtor",
    "-Woverloaded-virtual",
    "-Wself-assign",
    "-Wstring-conversion",
    "-Wtautological-overlap-compare",
    "-Wthread-safety-analysis",
    "-Wthread-safety-beta",
    "-Wunused-but-set-variable",
    "-Wunused-comparison",
    "-Wvla",
    # -Wextra compatibility between gcc and clang
    "-Wtype-limits",
    # A list of disabled flags coming from internal build system
    "-Wno-string-concatenation",
    # Exceptions but will be removed
    "-Wno-deprecated-declarations",
    "-Wno-unused-function",
]

GRPC_DEFAULT_COPTS = select({
    "//:use_strict_warning": GRPC_LLVM_WARNING_FLAGS + ["-DUSE_STRICT_WARNING=1"],
    "//conditions:default": [],
})
