# Copyright 2016 The Bazel Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# This becomes the BUILD file for @local_config_cc// under non-BSD unixes.

load(":cc_toolchain_config.bzl", "cc_toolchain_config")
load(":armeabi_cc_toolchain_config.bzl", "armeabi_cc_toolchain_config")
load("@rules_cc//cc:defs.bzl", "cc_toolchain", "cc_toolchain_suite")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # Apache 2.0

cc_library(
    name = "malloc",
)

filegroup(
    name = "empty",
    srcs = [],
)

filegroup(
    name = "cc_wrapper",
    srcs = ["cc_wrapper.sh"],
)

filegroup(
    name = "compiler_deps",
    srcs = glob(["extra_tools/**"], allow_empty = True) + [":builtin_include_directory_paths"],
)

# This is the entry point for --crosstool_top.  Toolchains are found
# by lopping off the name of --crosstool_top and searching for
# the "${CPU}" entry in the toolchains attribute.
cc_toolchain_suite(
    name = "toolchain",
    toolchains = {
        "k8|clang": ":cc-compiler-k8",
        "k8": ":cc-compiler-k8",
        "armeabi-v7a|compiler": ":cc-compiler-armeabi-v7a",
        "armeabi-v7a": ":cc-compiler-armeabi-v7a",
    },
)

cc_toolchain(
    name = "cc-compiler-k8",
    toolchain_identifier = "linux_gnu_x86",
    toolchain_config = ":linux_gnu_x86",
    all_files = ":compiler_deps",
    ar_files = ":compiler_deps",
    as_files = ":compiler_deps",
    compiler_files = ":compiler_deps",
    dwp_files = ":empty",
    linker_files = ":compiler_deps",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
    module_map = ":module.modulemap",
)

cc_toolchain_config(
    name = "linux_gnu_x86",
    cpu = "k8",
    compiler = "clang",
    toolchain_identifier = "linux_gnu_x86",
    host_system_name = "i686-unknown-linux-gnu",
    target_system_name = "x86_64-unknown-linux-gnu",
    target_libc = "glibc_2.19",
    abi_version = "clang",
    abi_libc_version = "glibc_2.19",
    cxx_builtin_include_directories = ["/usr/local/lib/clang/15.0.0/include",
    "/usr/local/include",
    "/usr/include/x86_64-linux-gnu",
    "/usr/include",
    "/usr/local/lib/clang/15.0.0/share",
    "/usr/include/c++/9",
    "/usr/include/x86_64-linux-gnu/c++/9",
    "/usr/include/c++/9/backward",
    "/usr/local/include/x86_64-unknown-linux-gnu/c++/v1",
    "/usr/local/include/c++/v1"],
    tool_paths = {"ar": "/usr/bin/ar",
        "ld": "/usr/bin/ld",
        "llvm-cov": "/usr/local/bin/llvm-cov",
        "cpp": "/usr/bin/cpp",
        "gcc": "/usr/local/bin/clang-15",
        "dwp": "/usr/bin/dwp",
        "gcov": "/dev/null",
        "nm": "/usr/bin/nm",
        "objcopy": "/usr/bin/objcopy",
        "objdump": "/usr/bin/objdump",
        "strip": "/usr/bin/strip"},
    compile_flags = ["-fstack-protector",
    "-Wall",
    "-Wthread-safety",
    "-Wself-assign",
    "-Wunused-but-set-parameter",
    "-Wno-free-nonheap-object",
    "-fcolor-diagnostics",
    "-fno-omit-frame-pointer"],
    opt_compile_flags = ["-g0",
    "-O2",
    "-D_FORTIFY_SOURCE=1",
    "-DNDEBUG",
    "-ffunction-sections",
    "-fdata-sections"],
    dbg_compile_flags = ["-g"],
    cxx_flags = ["-std=c++0x"],
    link_flags = ["-fuse-ld=/usr/local/bin/ld.lld",
    "-Wl,-no-as-needed",
    "-Wl,-z,relro,-z,now",
    "-B/usr/local/bin"],
    link_libs = ["-lstdc++",
    "-lm"],
    opt_link_flags = ["-Wl,--gc-sections"],
    unfiltered_compile_flags = ["-no-canonical-prefixes",
    "-Wno-builtin-macro-redefined",
    "-D__DATE__=\"redacted\"",
    "-D__TIMESTAMP__=\"redacted\"",
    "-D__TIME__=\"redacted\""],
    coverage_compile_flags = ["--coverage"],
    coverage_link_flags = ["--coverage"],
    supports_start_end_lib = True,
)

# Android tooling requires a default toolchain for the armeabi-v7a cpu.
cc_toolchain(
    name = "cc-compiler-armeabi-v7a",
    toolchain_identifier = "stub_armeabi-v7a",
    toolchain_config = ":stub_armeabi-v7a",
    all_files = ":empty",
    ar_files = ":empty",
    as_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
)

armeabi_cc_toolchain_config(name = "stub_armeabi-v7a")
