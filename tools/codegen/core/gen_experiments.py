#!/usr/bin/env python3

# Copyright 2022 gRPC authors.
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
"""Generate experiment related code artifacts.

Invoke as: tools/codegen/core/gen_experiments.py
Experiment definitions are in src/core/lib/experiments/experiments.bzl
"""

from __future__ import print_function

import argparse
import os
import sys

import experiments_compiler as exp

REPO_ROOT = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "../../..")
)

DEFAULTS = {
    "broken": "false",
    False: "false",
    True: "true",
    "debug": "kDefaultForDebugOnly",
}

PLATFORMS_DEFINE = {
    "windows": "GPR_WINDOWS",
    "ios": "GRPC_CFSTREAM",
    "posix": "",
}

FINAL_RETURN = {
    "broken": "return false;",
    False: "return false;",
    True: "return true;",
    "debug": "\n#ifdef NDEBUG\nreturn false;\n#else\nreturn true;\n#endif\n",
}

FINAL_DEFINE = {
    "broken": None,
    False: None,
    True: "#define %s",
    "debug": "#ifndef NDEBUG\n#define %s\n#endif",
}

BZL_LIST_FOR_DEFAULTS = {
    "broken": None,
    False: "off",
    True: "on",
    "debug": "dbg",
}


def ParseCommandLineArguments(args):
    """Wrapper for argparse command line arguments handling.

    Args:
    args: List of command line arguments.

    Returns:
    Command line arguments namespace built by argparse.ArgumentParser().
    """
    # formatter_class=argparse.ArgumentDefaultsHelpFormatter is not used here
    # intentionally, We want more formatting than this class can provide.
    flag_parser = argparse.ArgumentParser()
    flag_parser.add_argument(
        "--check",
        action="store_false",
        help="If specified, disables checking experiment expiry dates",
    )
    flag_parser.add_argument(
        "--no_dbg_experiments",
        action="store_true",
        help="Prohibit 'debug' configurations",
        default=False,
    )
    flag_parser.add_argument(
        "--experiment_definitions",
        type=str,
        default="src/core/lib/experiments/experiments.bzl",
        help="Specifies the file containing oss experiment definitions",
    )
    flag_parser.add_argument(
        "--rollout_definitions",
        type=str,
        default="src/core/lib/experiments/rollouts.bzl",
        help="Specifies the file containing rollout definitions",
    )
    flag_parser.add_argument(
        "--output_mode",
        type=str,
        default="production",
        choices=["production", "test"],
        help="If specified, only generate for this mode",
    )
    flag_parser.add_argument(
        "--exp_hdr_codegen_output_file",
        type=str,
        default="src/core/lib/experiments/experiments.h",
        help="Specifies the location where the generated hdr file is written",
    )
    flag_parser.add_argument(
        "--exp_src_codegen_output_file",
        type=str,
        default="src/core/lib/experiments/experiments.cc",
        help="Specifies the location where the generated src file is written",
    )
    flag_parser.add_argument(
        "--exp_test_codegen_output_file",
        type=str,
        default="test/core/experiments/experiments_test.cc",
        help="Specifies the location where the generated test file is written",
    )
    flag_parser.add_argument(
        "--disable_gen_hdrs",
        action="store_true",
        help="If specified, disables generation of experiments hdr files",
    )
    flag_parser.add_argument(
        "--disable_gen_srcs",
        action="store_true",
        help="If specified, disables generation of experiments source files",
    )
    flag_parser.add_argument(
        "--disable_gen_test",
        action="store_true",
        help="If specified, disables generation of experiments tests",
    )
    return flag_parser.parse_args(args)


args = ParseCommandLineArguments(sys.argv[1:])


def _InjectGithubPath(path):
    base, ext = os.path.splitext(path)
    return base + ".github" + ext


def _GenerateExperimentFiles(args, mode):
    if mode == "test":
        _EXPERIMENTS_DEFS = (
            "test/core/experiments/fixtures/test_experiments.bzl"
        )
        _EXPERIMENTS_ROLLOUTS = (
            "test/core/experiments/fixtures/test_experiments_rollout.bzl"
        )
        _EXPERIMENTS_HDR_FILE = "test/core/experiments/fixtures/experiments.h"
        _EXPERIMENTS_SRC_FILE = "test/core/experiments/fixtures/experiments.cc"
    else:
        _EXPERIMENTS_DEFS = "src/core/lib/experiments/experiments.bzl"
        _EXPERIMENTS_ROLLOUTS = "src/core/lib/experiments/rollouts.bzl"
        _EXPERIMENTS_HDR_FILE = "src/core/lib/experiments/experiments.h"
        _EXPERIMENTS_SRC_FILE = "src/core/lib/experiments/experiments.cc"

    # Override defaults if flags are provided
    if args.output_mode == mode or args.output_mode is None:
        if args.experiment_definitions and mode != "test":
            _EXPERIMENTS_DEFS = args.experiment_definitions
        if args.rollout_definitions and mode != "test":
            _EXPERIMENTS_ROLLOUTS = args.rollout_definitions

        if args.exp_hdr_codegen_output_file:
            _EXPERIMENTS_HDR_FILE = args.exp_hdr_codegen_output_file
        elif "/google3/" in REPO_ROOT and mode != "test":
            _EXPERIMENTS_HDR_FILE = _InjectGithubPath(_EXPERIMENTS_HDR_FILE)

        if args.exp_src_codegen_output_file:
            _EXPERIMENTS_SRC_FILE = args.exp_src_codegen_output_file
        elif "/google3/" in REPO_ROOT and mode != "test":
            _EXPERIMENTS_SRC_FILE = _InjectGithubPath(_EXPERIMENTS_SRC_FILE)

    compiler = exp.ExperimentsCompiler(
        DEFAULTS,
        FINAL_RETURN,
        FINAL_DEFINE,
        PLATFORMS_DEFINE,
        BZL_LIST_FOR_DEFAULTS,
    )

    if not compiler.LoadExperimentsFromBzl(
        _EXPERIMENTS_DEFS, "EXPERIMENTS", args.check
    ):
        sys.exit(1)

    if not compiler.LoadRolloutsFromBzl(_EXPERIMENTS_ROLLOUTS, "ROLLOUTS"):
        sys.exit(1)

    experiment_annotation = "gRPC Experiments: "
    for name in compiler.experiment_names:
        experiment_annotation += name + ":0,"

    if len(experiment_annotation) > 2000:
        print("comma-delimited string of experiments is too long")
        sys.exit(1)

    if mode != "test" and args.no_dbg_experiments:
        print("Ensuring no debug experiments are configured")
        compiler.EnsureNoDebugExperiments()

    if not args.disable_gen_hdrs:
        print(
            f"Mode = {mode} Generating experiments headers into"
            f" {_EXPERIMENTS_HDR_FILE}"
        )
        compiler.GenerateExperimentsHdr(_EXPERIMENTS_HDR_FILE, mode)

    if not args.disable_gen_srcs:
        print(
            f"Mode = {mode} Generating experiments srcs into"
            f" {_EXPERIMENTS_SRC_FILE}"
        )
        compiler.GenerateExperimentsSrc(
            _EXPERIMENTS_SRC_FILE, _EXPERIMENTS_HDR_FILE, mode
        )

    if mode == "test" and not args.disable_gen_test:
        print(
            "Generating experiments tests into"
            f" {args.exp_test_codegen_output_file}"
        )
        compiler.GenTest(args.exp_test_codegen_output_file)


_GenerateExperimentFiles(args, args.output_mode)
