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
"""
A module to assist in generating experiment related code and artifacts.
"""

from __future__ import print_function

import collections
import ctypes
import datetime
import json
import math
import os
import re
import sys

import yaml

_CODEGEN_PLACEHOLDER_TEXT = """
This file contains the autogenerated parts of the experiments API.

It generates two symbols for each experiment.

For the experiment named new_car_project, it generates:

- a function IsNewCarProjectEnabled() that returns true if the experiment
  should be enabled at runtime.

- a macro GRPC_EXPERIMENT_IS_INCLUDED_NEW_CAR_PROJECT that is defined if the
  experiment *could* be enabled at runtime.

The function is used to determine whether to run the experiment or
non-experiment code path.

If the experiment brings significant bloat, the macro can be used to avoid
including the experiment code path in the binary for binaries that are size
sensitive.

By default that includes our iOS and Android builds.

Finally, a small array is included that contains the metadata for each
experiment.

A macro, GRPC_EXPERIMENTS_ARE_FINAL, controls whether we fix experiment
configuration at build time (if it's defined) or allow it to be tuned at
runtime (if it's disabled).

If you are using the Bazel build system, that macro can be configured with
--define=grpc_experiments_are_final=true
"""


def _EXPERIMENTS_TEST_SKELETON(defs, test_body):
    return f"""
#include <grpc/support/port_platform.h>

#include "test/core/experiments/fixtures/experiments.h"
#include "gtest/gtest.h"

#include "src/core/lib/experiments/config.h"

#ifndef GRPC_EXPERIMENTS_ARE_FINAL
{defs}
TEST(ExperimentsTest, CheckExperimentValuesTest) {{
{test_body}
}}

#endif // GRPC_EXPERIMENTS_ARE_FINAL

int main(int argc, char** argv) {{
  testing::InitGoogleTest(&argc, argv);
  grpc_core::LoadTestOnlyExperimentsFromMetadata(
    grpc_core::g_test_experiment_metadata, grpc_core::kNumTestExperiments);
  return RUN_ALL_TESTS();
}}
"""


def _EXPERIMENTS_EXPECTED_VALUE(name, expected_value):
    return f"""
bool GetExperiment{name}ExpectedValue() {{
{expected_value}
}}
"""


def _EXPERIMENT_CHECK_TEXT(name):
    return f"""
  ASSERT_EQ(grpc_core::Is{name}Enabled(),
            GetExperiment{name}ExpectedValue());
"""


def ToCStr(s, encoding="ascii"):
    if isinstance(s, str):
        s = s.encode(encoding)
    result = ""
    for c in s:
        c = chr(c) if isinstance(c, int) else c
        if not (32 <= ord(c) < 127) or c in ("\\", '"'):
            result += "\\%03o" % ord(c)
        else:
            result += c
    return '"' + result + '"'


def SnakeToPascal(s):
    return "".join(x.capitalize() for x in s.split("_"))


def PutBanner(files, banner, prefix):
    # Print a big comment block into a set of files
    for f in files:
        for line in banner:
            if not line:
                print(prefix, file=f)
            else:
                print("%s %s" % (prefix, line), file=f)
        print(file=f)


def PutCopyright(file, prefix):
    # copy-paste copyright notice from this file
    with open(__file__) as my_source:
        copyright = []
        for line in my_source:
            if line[0] != "#":
                break
        for line in my_source:
            if line[0] == "#":
                copyright.append(line)
                break
        for line in my_source:
            if line[0] != "#":
                break
            copyright.append(line)
        PutBanner([file], [line[2:].rstrip() for line in copyright], prefix)


class ExperimentDefinition(object):
    def __init__(self, attributes):
        self._error = False
        if "name" not in attributes:
            print("ERROR: experiment with no name: %r" % attributes)
            self._error = True
        if "description" not in attributes:
            print(
                "ERROR: no description for experiment %s" % attributes["name"]
            )
            self._error = True
        if "owner" not in attributes:
            print("ERROR: no owner for experiment %s" % attributes["name"])
            self._error = True
        if "expiry" not in attributes:
            print("ERROR: no expiry for experiment %s" % attributes["name"])
            self._error = True
        if attributes["name"] == "monitoring_experiment":
            if attributes["expiry"] != "never-ever":
                print("ERROR: monitoring_experiment should never expire")
                self._error = True
        if self._error:
            print("Failed to create experiment definition")
            return
        self._allow_in_fuzzing_config = True
        self._name = attributes["name"]
        self._description = attributes["description"]
        self._expiry = attributes["expiry"]
        self._default = {}
        self._additional_constraints = {}
        self._test_tags = []

        if "allow_in_fuzzing_config" in attributes:
            self._allow_in_fuzzing_config = attributes[
                "allow_in_fuzzing_config"
            ]

        if "test_tags" in attributes:
            self._test_tags = attributes["test_tags"]

    def IsValid(self, check_expiry=False):
        if self._error:
            return False
        if not check_expiry:
            return True
        if (
            self._name == "monitoring_experiment"
            and self._expiry == "never-ever"
        ):
            return True
        today = datetime.date.today()
        two_quarters_from_now = today + datetime.timedelta(days=180)
        expiry = datetime.datetime.strptime(self._expiry, "%Y/%m/%d").date()
        if expiry < today:
            print(
                "WARNING: experiment %s expired on %s"
                % (self._name, self._expiry)
            )
        if expiry > two_quarters_from_now:
            print(
                "WARNING: experiment %s expires far in the future on %s"
                % (self._name, self._expiry)
            )
            print("expiry should be no more than two quarters from now")
        return not self._error

    def AddRolloutSpecification(
        self, allowed_defaults, allowed_platforms, rollout_attributes
    ):
        if self._error:
            return False
        if rollout_attributes["name"] != self._name:
            print(
                "ERROR: Rollout specification does not apply to this"
                " experiment: %s" % self._name
            )
            return False
        if "default" not in rollout_attributes:
            print(
                "ERROR: no default for experiment %s"
                % rollout_attributes["name"]
            )
            self._error = True
            return False
        is_dict = isinstance(rollout_attributes["default"], dict)
        for platform in allowed_platforms:
            if is_dict:
                value = rollout_attributes["default"].get(platform, False)
            else:
                value = rollout_attributes["default"]
            if isinstance(value, dict):
                self._default[platform] = "debug"
                self._additional_constraints[platform] = value
            elif value not in allowed_defaults:
                print(
                    "ERROR: default for experiment %s on platform %s "
                    "is of incorrect format"
                    % (rollout_attributes["name"], platform)
                )
                self._error = True
                return False
            else:
                self._default[platform] = value
                self._additional_constraints[platform] = {}
        return True

    @property
    def name(self):
        return self._name

    @property
    def description(self):
        return self._description

    def default(self, platform):
        return self._default.get(platform, False)

    @property
    def test_tags(self):
        return self._test_tags

    @property
    def allow_in_fuzzing_config(self):
        return self._allow_in_fuzzing_config

    def additional_constraints(self, platform):
        return self._additional_constraints.get(platform, {})


class ExperimentsCompiler(object):
    def __init__(
        self,
        defaults,
        final_return,
        final_define,
        platforms_define,
        bzl_list_for_defaults=None,
    ):
        self._defaults = defaults
        self._final_return = final_return
        self._final_define = final_define
        self._platforms_define = platforms_define
        self._bzl_list_for_defaults = bzl_list_for_defaults
        self._experiment_definitions = {}
        self._experiment_rollouts = {}

    def AddExperimentDefinition(self, experiment_definition):
        if experiment_definition.name in self._experiment_definitions:
            print(
                "ERROR: Duplicate experiment definition: %s"
                % experiment_definition.name
            )
            return False
        self._experiment_definitions[
            experiment_definition.name
        ] = experiment_definition
        return True

    def AddRolloutSpecification(self, rollout_attributes):
        if "name" not in rollout_attributes:
            print(
                "ERROR: experiment with no name: %r in rollout_attribute"
                % rollout_attributes
            )
            return False
        if rollout_attributes["name"] not in self._experiment_definitions:
            print(
                "WARNING: rollout for an undefined experiment: %s ignored"
                % rollout_attributes["name"]
            )
            return True
        return self._experiment_definitions[
            rollout_attributes["name"]
        ].AddRolloutSpecification(
            self._defaults, self._platforms_define, rollout_attributes
        )

    def _GenerateExperimentsHdrForPlatform(self, platform, file_desc):
        for _, exp in self._experiment_definitions.items():
            define_fmt = self._final_define[exp.default(platform)]
            if define_fmt:
                print(
                    define_fmt
                    % ("GRPC_EXPERIMENT_IS_INCLUDED_%s" % exp.name.upper()),
                    file=file_desc,
                )
            print(
                "inline bool Is%sEnabled() { %s }"
                % (
                    SnakeToPascal(exp.name),
                    self._final_return[exp.default(platform)],
                ),
                file=file_desc,
            )

    def GenerateExperimentsHdr(self, output_file, mode):
        with open(output_file, "w") as H:
            PutCopyright(H, "//")
            PutBanner(
                [H],
                ["Auto generated by tools/codegen/core/gen_experiments.py"]
                + _CODEGEN_PLACEHOLDER_TEXT.splitlines(),
                "//",
            )

            if mode != "test":
                include_guard = "GRPC_SRC_CORE_LIB_EXPERIMENTS_EXPERIMENTS_H"
            else:
                file_path_list = output_file.split("/")[0:-1]
                file_name = output_file.split("/")[-1].split(".")[0]

                include_guard = f"GRPC_{'_'.join(path.upper() for path in file_path_list)}_{file_name.upper()}_H"

            print(f"#ifndef {include_guard}", file=H)
            print(f"#define {include_guard}", file=H)
            print(file=H)
            print("#include <grpc/support/port_platform.h>", file=H)
            print(file=H)
            print("#include <stddef.h>", file=H)
            print('#include "src/core/lib/experiments/config.h"', file=H)
            print(file=H)
            print("namespace grpc_core {", file=H)
            print(file=H)
            print("#ifdef GRPC_EXPERIMENTS_ARE_FINAL", file=H)
            idx = 0
            for platform in sorted(self._platforms_define.keys()):
                if platform == "posix":
                    continue
                print(
                    f"\n#{'if' if idx ==0 else 'elif'} "
                    f"defined({self._platforms_define[platform]})",
                    file=H,
                )
                self._GenerateExperimentsHdrForPlatform(platform, H)
                idx += 1
            print("\n#else", file=H)
            self._GenerateExperimentsHdrForPlatform("posix", H)
            print("#endif", file=H)
            print("\n#else", file=H)
            for i, (_, exp) in enumerate(self._experiment_definitions.items()):
                print(
                    "#define GRPC_EXPERIMENT_IS_INCLUDED_%s" % exp.name.upper(),
                    file=H,
                )
                print(
                    "inline bool Is%sEnabled() { return"
                    " Is%sExperimentEnabled(%d); }"
                    % (
                        SnakeToPascal(exp.name),
                        "Test" if mode == "test" else "",
                        i,
                    ),
                    file=H,
                )
            print(file=H)

            if mode == "test":
                num_experiments_var_name = "kNumTestExperiments"
                experiments_metadata_var_name = "g_test_experiment_metadata"
            else:
                num_experiments_var_name = "kNumExperiments"
                experiments_metadata_var_name = "g_experiment_metadata"
            print(
                f"constexpr const size_t {num_experiments_var_name} = "
                f"{len(self._experiment_definitions.keys())};",
                file=H,
            )
            print(
                (
                    "extern const ExperimentMetadata"
                    f" {experiments_metadata_var_name}[{num_experiments_var_name}];"
                ),
                file=H,
            )
            print(file=H)
            print("#endif", file=H)
            print("}  // namespace grpc_core", file=H)
            print(file=H)
            print(f"#endif  // {include_guard}", file=H)

    def _GenerateExperimentsSrcForPlatform(self, platform, mode, file_desc):
        print("namespace {", file=file_desc)
        have_defaults = set()
        for _, exp in self._experiment_definitions.items():
            print(
                "const char* const description_%s = %s;"
                % (exp.name, ToCStr(exp.description)),
                file=file_desc,
            )
            print(
                "const char* const additional_constraints_%s = %s;"
                % (
                    exp.name,
                    ToCStr(json.dumps(exp.additional_constraints(platform))),
                ),
                file=file_desc,
            )
            have_defaults.add(self._defaults[exp.default(platform)])
        if "kDefaultForDebugOnly" in have_defaults:
            print("#ifdef NDEBUG", file=file_desc)
            if "kDefaultForDebugOnly" in have_defaults:
                print(
                    "const bool kDefaultForDebugOnly = false;", file=file_desc
                )
            print("#else", file=file_desc)
            if "kDefaultForDebugOnly" in have_defaults:
                print("const bool kDefaultForDebugOnly = true;", file=file_desc)
            print("#endif", file=file_desc)
        print("}", file=file_desc)
        print(file=file_desc)
        print("namespace grpc_core {", file=file_desc)
        print(file=file_desc)
        if mode == "test":
            experiments_metadata_var_name = "g_test_experiment_metadata"
        else:
            experiments_metadata_var_name = "g_experiment_metadata"
        print(
            f"const ExperimentMetadata {experiments_metadata_var_name}[] = {{",
            file=file_desc,
        )
        for _, exp in self._experiment_definitions.items():
            print(
                "  {%s, description_%s, additional_constraints_%s, %s, %s},"
                % (
                    ToCStr(exp.name),
                    exp.name,
                    exp.name,
                    self._defaults[exp.default(platform)],
                    "true" if exp.allow_in_fuzzing_config else "false",
                ),
                file=file_desc,
            )
        print("};", file=file_desc)
        print(file=file_desc)
        print("}  // namespace grpc_core", file=file_desc)

    def GenerateExperimentsSrc(self, output_file, header_file_path, mode):
        with open(output_file, "w") as C:
            PutCopyright(C, "//")
            PutBanner(
                [C],
                ["Auto generated by tools/codegen/core/gen_experiments.py"],
                "//",
            )

            print("#include <grpc/support/port_platform.h>", file=C)
            print(f'#include "{header_file_path}"', file=C)
            print(file=C)
            print("#ifndef GRPC_EXPERIMENTS_ARE_FINAL", file=C)
            idx = 0
            for platform in sorted(self._platforms_define.keys()):
                if platform == "posix":
                    continue
                print(
                    f"\n#{'if' if idx ==0 else 'elif'} "
                    f"defined({self._platforms_define[platform]})",
                    file=C,
                )
                self._GenerateExperimentsSrcForPlatform(platform, mode, C)
                idx += 1
            print("\n#else", file=C)
            self._GenerateExperimentsSrcForPlatform("posix", mode, C)
            print("#endif", file=C)
            print("#endif", file=C)

    def _GenTestExperimentsExpectedValues(self, platform):
        defs = ""
        for _, exp in self._experiment_definitions.items():
            defs += _EXPERIMENTS_EXPECTED_VALUE(
                SnakeToPascal(exp.name),
                self._final_return[exp.default(platform)],
            )
        return defs

    def GenTest(self, output_file):
        with open(output_file, "w") as C:
            PutCopyright(C, "//")
            PutBanner(
                [C],
                ["Auto generated by tools/codegen/core/gen_experiments.py"],
                "//",
            )
            defs = ""
            test_body = ""
            idx = 0
            for platform in sorted(self._platforms_define.keys()):
                if platform == "posix":
                    continue
                defs += (
                    f"\n#{'if' if idx ==0 else 'elif'} "
                    f"defined({self._platforms_define[platform]})"
                )
                defs += self._GenTestExperimentsExpectedValues(platform)
                idx += 1
            defs += "\n#else"
            defs += self._GenTestExperimentsExpectedValues("posix")
            defs += "#endif\n"
            for _, exp in self._experiment_definitions.items():
                test_body += _EXPERIMENT_CHECK_TEXT(SnakeToPascal(exp.name))
            print(_EXPERIMENTS_TEST_SKELETON(defs, test_body), file=C)

    def GenExperimentsBzl(self, output_file):
        if self._bzl_list_for_defaults is None:
            return

        bzl_to_tags_to_experiments = dict((platform, dict(
            (key, collections.defaultdict(list))
            for key in self._bzl_list_for_defaults.keys()
            if key is not None
        )) for platform in self._platforms_define.keys())

        for platform in self._platforms_define.keys():
            for _, exp in self._experiment_definitions.items():
                for tag in exp.test_tags:
                    # Search through default values for all platforms.
                    default = exp.default(platform)
                    # Interpret the debug default value as True to switch the
                    # experiment to the "on" mode.
                    if default == "debug":
                        default = True
                    bzl_to_tags_to_experiments[platform][default][tag].append(exp.name)

        with open(output_file, "w") as B:
            PutCopyright(B, "#")
            PutBanner(
                [B],
                ["Auto generated by tools/codegen/core/gen_experiments.py"],
                "#",
            )

            print(
                (
                    '"""Dictionary of tags to experiments so we know when to'
                    ' test different experiments."""'
                ),
                file=B,
            )

            print(file=B)
            print("EXPERIMENTS = {", file=B)

            print (bzl_to_tags_to_experiments['windows'].items())
            for platform in self._platforms_define.keys():
                bzl_to_tags_to_experiments_platform = sorted(
                    (self._bzl_list_for_defaults[default], tags_to_experiments)
                    for default, tags_to_experiments in bzl_to_tags_to_experiments[platform].items()
                    if self._bzl_list_for_defaults[default] is not None
                )
                print('    "%s": {' % platform, file=B)
                for key, tags_to_experiments in bzl_to_tags_to_experiments_platform:
                    print('        "%s": {' % key, file=B)
                    for tag, experiments in sorted(tags_to_experiments.items()):
                        print('            "%s": [' % tag, file=B)
                        for experiment in sorted(experiments):
                            print('                "%s",' % experiment, file=B)
                        print("            ],", file=B)
                    print("        },", file=B)
                print("    },", file=B)
            print("}", file=B)
