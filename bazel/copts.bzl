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

# This is a list of llvm flags to be used when being built with use_strict_warning=1
GRPC_LLVM_WARNING_FLAGS = [
    # Enable all & extra waninrgs
    "-Wall",
    "-Wextra",
    # Consider warnings as errors
    "-Werror",
    # Ignore unknown warning flags
    "-Wno-unknown-warning-option",
    # A list of flags coming from internal build system
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
    "-Wunused-comparison",
    "-Wvla",
    # Exceptions but will be removed
    "-Wno-deprecated-declarations",
    "-Wno-unused-function",
]

GRPC_DEFAULT_COPTS = select({
    "//:use_strict_warning": GRPC_LLVM_WARNING_FLAGS,
    "//conditions:default": [],
})
