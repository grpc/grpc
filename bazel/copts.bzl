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
    # alignment issues
    "-Walign-mismatch",
    "-Wover-aligned",
    "-Wunaligned-access",
]

GRPC_LLVM_WINDOWS_WARNING_FLAGS = GRPC_LLVM_WARNING_FLAGS + [
    ### Some checks that are missing with clang-cl
    "-Wthread-safety",
    "-Wreorder-ctor",

    ### Avoid some checks that are default on clang-cl
    "-Wno-c++98-compat-pedantic",
    "-Wno-missing-prototypes",
    "-Wno-thread-safety-precise",  # too many aliases

    # abseil offenses
    "-Wno-comma",
    "-Wno-deprecated-redundant-constexpr-static-def",
    "-Wno-deprecated",  # remove when the above works in all clang versions we test with
    "-Wno-float-equal",
    "-Wno-gcc-compat",
    "-Wno-reserved-identifier",
    "-Wno-thread-safety-negative",
    "-Wno-sign-compare",

    # re2 offenses
    "-Wno-zero-as-null-pointer-constant",

    # ares offenses
    "-Wno-macro-redefined",

    # protobuf offenses
    "-Wno-cast-align",
    "-Wno-inconsistent-missing-destructor-override",

    # xxhash offenses
    "-Wno-disabled-macro-expansion",

    # benchmark offenses
    "-Wno-shift-sign-overflow",

    # Evidently nodiscard is inappropriately pinned as a C++17 feature
    "-Wno-c++17-attribute-extensions",

    # declarations are not used in many places
    "-Wno-missing-variable-declarations",

    # TODO: delete iomgr
    "-Wno-old-style-cast",
    "-Wno-cast-qual",
    "-Wno-unused-member-function",
    "-Wno-unused-template",

    # TODO(hork): see if the TraceFlag offense can be removed
    "-Wno-global-constructors",
    # TODO(hork): clean up EE offenses
    "-Wno-missing-field-initializers",
    "-Wno-non-virtual-dtor",
    "-Wno-thread-safety-reference-return",

    # TODO(ctiller): offense: dump_args. signed to unsigned
    "-Wno-sign-conversion",
    "-Wno-shorten-64-to-32",

    # TODO: general cleanup required. Maybe new developer or rainy day projects.
    "-Wno-unreachable-code-break",
    "-Wno-unreachable-code-return",
    "-Wno-unreachable-code",
    "-Wno-used-but-marked-unused",
    "-Wno-newline-eof",
    "-Wno-unused-const-variable",
    "-Wno-extra-semi",
    "-Wno-extra-semi-stmt",
    "-Wno-suggest-destructor-override",
    "-Wno-shadow",
    "-Wno-missing-noreturn",
    "-Wno-nested-anon-types",
    "-Wno-gnu-anonymous-struct",
    "-Wno-nonportable-system-include-path",
    "-Wno-microsoft-cast",
    "-Wno-exit-time-destructors",
    "-Wno-undef",  # #if <MACRO_NAME> to #ifdef <MACRO_NAME>
    "-Wno-unused-macros",
    "-Wno-redundant-parens",
    "-Wno-undefined-func-template",
    "-Wno-gnu-zero-variadic-macro-arguments",
    "-Wno-double-promotion",
    "-Wno-implicit-float-conversion",
    "-Wno-implicit-int-conversion",
    "-Wno-float-conversion",
    "-Wno-unused-parameter",
    "-Wno-suggest-override",
    "-Wno-documentation",
    "-Wno-documentation-unknown-command",
    "-Wno-unsafe-buffer-usage",

    ### possibly bad warnings for this codebase
    "-Wno-covered-switch-default",
    "-Wno-switch-default",
    "-Wno-switch-enum",
    "-Wno-c99-extensions",
    "-Wno-unused-private-field",  # GRPC_UNUSED does not appear to work for private fields
]

GRPC_DEFAULT_COPTS = select({
    "//:use_strict_warning": GRPC_LLVM_WARNING_FLAGS + ["-DUSE_STRICT_WARNING=1"],
    "//:use_strict_warning_windows": GRPC_LLVM_WINDOWS_WARNING_FLAGS + ["-DUSE_STRICT_WARNING=1"],
    "//conditions:default": [],
})
