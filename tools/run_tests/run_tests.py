#!/usr/bin/env python3
# Copyright 2015 gRPC authors.
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
"""Run tests in parallel."""

from __future__ import print_function

import argparse
import ast
import collections
import glob
import itertools
import json
import logging
import multiprocessing
import os
import os.path
import pipes
import platform
import random
import re
import socket
import subprocess
import sys
import tempfile
import time
import traceback
import uuid

import six
from six.moves import urllib

import python_utils.jobset as jobset
import python_utils.report_utils as report_utils
import python_utils.start_port_server as start_port_server
import python_utils.watch_dirs as watch_dirs

try:
    from python_utils.upload_test_results import upload_results_to_bq
except ImportError:
    pass  # It's ok to not import because this is only necessary to upload results to BQ.

gcp_utils_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../gcp/utils")
)
sys.path.append(gcp_utils_dir)

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "../.."))
os.chdir(_ROOT)

_FORCE_ENVIRON_FOR_WRAPPERS = {
    "GRPC_VERBOSITY": "DEBUG",
}

_POLLING_STRATEGIES = {
    "linux": ["epoll1", "poll"],
    "mac": ["poll"],
}


def platform_string():
    return jobset.platform_string()


_DEFAULT_TIMEOUT_SECONDS = 5 * 60
_PRE_BUILD_STEP_TIMEOUT_SECONDS = 10 * 60


def run_shell_command(cmd, env=None, cwd=None):
    try:
        subprocess.check_output(cmd, shell=True, env=env, cwd=cwd)
    except subprocess.CalledProcessError as e:
        logging.exception(
            "Error while running command '%s'. Exit status %d. Output:\n%s",
            e.cmd,
            e.returncode,
            e.output,
        )
        raise


def max_parallel_tests_for_current_platform():
    # Too much test parallelization has only been seen to be a problem
    # so far on windows.
    if jobset.platform_string() == "windows":
        return 64
    return 1024


def _print_debug_info_epilogue(dockerfile_dir=None):
    """Use to print useful info for debug/repro just before exiting."""
    print("")
    print("=== run_tests.py DEBUG INFO ===")
    print('command: "%s"' % " ".join(sys.argv))
    if dockerfile_dir:
        print("dockerfile: %s" % dockerfile_dir)
    kokoro_job_name = os.getenv("KOKORO_JOB_NAME")
    if kokoro_job_name:
        print("kokoro job name: %s" % kokoro_job_name)
    print("===============================")


# SimpleConfig: just compile with CONFIG=config, and run the binary to test
class Config(object):
    def __init__(
        self,
        config,
        environ=None,
        timeout_multiplier=1,
        tool_prefix=[],
        iomgr_platform="native",
    ):
        if environ is None:
            environ = {}
        self.build_config = config
        self.environ = environ
        self.environ["CONFIG"] = config
        self.tool_prefix = tool_prefix
        self.timeout_multiplier = timeout_multiplier
        self.iomgr_platform = iomgr_platform

    def job_spec(
        self,
        cmdline,
        timeout_seconds=_DEFAULT_TIMEOUT_SECONDS,
        shortname=None,
        environ={},
        cpu_cost=1.0,
        flaky=False,
    ):
        """Construct a jobset.JobSpec for a test under this config

        Args:
          cmdline:      a list of strings specifying the command line the test
                        would like to run
        """
        actual_environ = self.environ.copy()
        for k, v in environ.items():
            actual_environ[k] = v
        if not flaky and shortname and shortname in flaky_tests:
            flaky = True
        if shortname in shortname_to_cpu:
            cpu_cost = shortname_to_cpu[shortname]
        return jobset.JobSpec(
            cmdline=self.tool_prefix + cmdline,
            shortname=shortname,
            environ=actual_environ,
            cpu_cost=cpu_cost,
            timeout_seconds=(
                self.timeout_multiplier * timeout_seconds
                if timeout_seconds
                else None
            ),
            flake_retries=4 if flaky or args.allow_flakes else 0,
            timeout_retries=1 if flaky or args.allow_flakes else 0,
        )


def get_c_tests(travis, test_lang):
    out = []
    platforms_str = "ci_platforms" if travis else "platforms"
    with open("tools/run_tests/generated/tests.json") as f:
        js = json.load(f)
        return [
            tgt
            for tgt in js
            if tgt["language"] == test_lang
            and platform_string() in tgt[platforms_str]
            and not (travis and tgt["flaky"])
        ]


def _check_compiler(compiler, supported_compilers):
    if compiler not in supported_compilers:
        raise Exception(
            "Compiler %s not supported (on this platform)." % compiler
        )


def _check_arch(arch, supported_archs):
    if arch not in supported_archs:
        raise Exception("Architecture %s not supported." % arch)


def _is_use_docker_child():
    """Returns True if running running as a --use_docker child."""
    return True if os.getenv("DOCKER_RUN_SCRIPT_COMMAND") else False


_PythonConfigVars = collections.namedtuple(
    "_ConfigVars",
    [
        "shell",
        "builder",
        "builder_prefix_arguments",
        "venv_relative_python",
        "toolchain",
        "runner",
    ],
)


def _python_config_generator(name, major, minor, bits, config_vars):
    build = (
        config_vars.shell
        + config_vars.builder
        + config_vars.builder_prefix_arguments
        + [_python_pattern_function(major=major, minor=minor, bits=bits)]
        + [name]
        + config_vars.venv_relative_python
        + config_vars.toolchain
    )
    run = (
        config_vars.shell
        + config_vars.runner
        + [
            os.path.join(name, config_vars.venv_relative_python[0]),
        ]
    )
    return PythonConfig(name, build, run)


def _pypy_config_generator(name, major, config_vars):
    return PythonConfig(
        name,
        config_vars.shell
        + config_vars.builder
        + config_vars.builder_prefix_arguments
        + [_pypy_pattern_function(major=major)]
        + [name]
        + config_vars.venv_relative_python
        + config_vars.toolchain,
        config_vars.shell
        + config_vars.runner
        + [os.path.join(name, config_vars.venv_relative_python[0])],
    )


def _python_pattern_function(major, minor, bits):
    # Bit-ness is handled by the test machine's environment
    if os.name == "nt":
        if bits == "64":
            return "/c/Python{major}{minor}/python.exe".format(
                major=major, minor=minor, bits=bits
            )
        else:
            return "/c/Python{major}{minor}_{bits}bits/python.exe".format(
                major=major, minor=minor, bits=bits
            )
    else:
        return "python{major}.{minor}".format(major=major, minor=minor)


def _pypy_pattern_function(major):
    if major == "2":
        return "pypy"
    elif major == "3":
        return "pypy3"
    else:
        raise ValueError("Unknown PyPy major version")


class CLanguage(object):
    def __init__(self, lang_suffix, test_lang):
        self.lang_suffix = lang_suffix
        self.platform = platform_string()
        self.test_lang = test_lang

    def configure(self, config, args):
        self.config = config
        self.args = args
        if self.platform == "windows":
            _check_compiler(
                self.args.compiler,
                [
                    "default",
                    "cmake",
                    "cmake_ninja_vs2019",
                    "cmake_vs2019",
                ],
            )
            _check_arch(self.args.arch, ["default", "x64", "x86"])

            activate_vs_tools = ""
            if (
                self.args.compiler == "cmake_ninja_vs2019"
                or self.args.compiler == "cmake"
                or self.args.compiler == "default"
            ):
                # cmake + ninja build is the default because it is faster and supports boringssl assembly optimizations
                # the compiler used is exactly the same as for cmake_vs2017
                cmake_generator = "Ninja"
                activate_vs_tools = "2019"
            elif self.args.compiler == "cmake_vs2019":
                cmake_generator = "Visual Studio 16 2019"
            else:
                print("should never reach here.")
                sys.exit(1)

            self._cmake_configure_extra_args = []
            self._cmake_generator_windows = cmake_generator
            # required to pass as cmake "-A" configuration for VS builds (but not for Ninja)
            self._cmake_architecture_windows = (
                "x64" if self.args.arch == "x64" else "Win32"
            )
            # when builing with Ninja, the VS common tools need to be activated first
            self._activate_vs_tools_windows = activate_vs_tools
            # "x64_x86" means create 32bit binaries, but use 64bit toolkit to secure more memory for the build
            self._vs_tools_architecture_windows = (
                "x64" if self.args.arch == "x64" else "x64_x86"
            )

        else:
            if self.platform == "linux":
                # Allow all the known architectures. _check_arch_option has already checked that we're not doing
                # something illegal when not running under docker.
                _check_arch(self.args.arch, ["default", "x64", "x86", "arm64"])
            else:
                _check_arch(self.args.arch, ["default"])

            (
                self._docker_distro,
                self._cmake_configure_extra_args,
            ) = self._compiler_options(self.args.use_docker, self.args.compiler)

    def test_specs(self):
        out = []
        binaries = get_c_tests(self.args.travis, self.test_lang)
        for target in binaries:
            if target.get("boringssl", False):
                # cmake doesn't build boringssl tests
                continue
            auto_timeout_scaling = target.get("auto_timeout_scaling", True)
            polling_strategies = (
                _POLLING_STRATEGIES.get(self.platform, ["all"])
                if target.get("uses_polling", True)
                else ["none"]
            )
            for polling_strategy in polling_strategies:
                env = {
                    "GRPC_DEFAULT_SSL_ROOTS_FILE_PATH": _ROOT
                    + "/src/core/tsi/test_creds/ca.pem",
                    "GRPC_POLL_STRATEGY": polling_strategy,
                    "GRPC_VERBOSITY": "DEBUG",
                }
                resolver = os.environ.get("GRPC_DNS_RESOLVER", None)
                if resolver:
                    env["GRPC_DNS_RESOLVER"] = resolver
                shortname_ext = (
                    ""
                    if polling_strategy == "all"
                    else " GRPC_POLL_STRATEGY=%s" % polling_strategy
                )
                if polling_strategy in target.get("excluded_poll_engines", []):
                    continue

                timeout_scaling = 1
                if auto_timeout_scaling:
                    config = self.args.config
                    if (
                        "asan" in config
                        or config == "msan"
                        or config == "tsan"
                        or config == "ubsan"
                        or config == "helgrind"
                        or config == "memcheck"
                    ):
                        # Scale overall test timeout if running under various sanitizers.
                        # scaling value is based on historical data analysis
                        timeout_scaling *= 3

                if self.config.build_config in target["exclude_configs"]:
                    continue
                if self.args.iomgr_platform in target.get("exclude_iomgrs", []):
                    continue

                if self.platform == "windows":
                    if self._cmake_generator_windows == "Ninja":
                        binary = "cmake/build/%s.exe" % target["name"]
                    else:
                        binary = "cmake/build/%s/%s.exe" % (
                            _MSBUILD_CONFIG[self.config.build_config],
                            target["name"],
                        )
                else:
                    binary = "cmake/build/%s" % target["name"]

                cpu_cost = target["cpu_cost"]
                if cpu_cost == "capacity":
                    cpu_cost = multiprocessing.cpu_count()
                if os.path.isfile(binary):
                    list_test_command = None
                    filter_test_command = None

                    # these are the flag defined by gtest and benchmark framework to list
                    # and filter test runs. We use them to split each individual test
                    # into its own JobSpec, and thus into its own process.
                    if "benchmark" in target and target["benchmark"]:
                        with open(os.devnull, "w") as fnull:
                            tests = subprocess.check_output(
                                [binary, "--benchmark_list_tests"], stderr=fnull
                            )
                        for line in tests.decode().split("\n"):
                            test = line.strip()
                            if not test:
                                continue
                            cmdline = [
                                binary,
                                "--benchmark_filter=%s$" % test,
                            ] + target["args"]
                            out.append(
                                self.config.job_spec(
                                    cmdline,
                                    shortname="%s %s"
                                    % (" ".join(cmdline), shortname_ext),
                                    cpu_cost=cpu_cost,
                                    timeout_seconds=target.get(
                                        "timeout_seconds",
                                        _DEFAULT_TIMEOUT_SECONDS,
                                    )
                                    * timeout_scaling,
                                    environ=env,
                                )
                            )
                    elif "gtest" in target and target["gtest"]:
                        # here we parse the output of --gtest_list_tests to build up a complete
                        # list of the tests contained in a binary for each test, we then
                        # add a job to run, filtering for just that test.
                        with open(os.devnull, "w") as fnull:
                            tests = subprocess.check_output(
                                [binary, "--gtest_list_tests"], stderr=fnull
                            )
                        base = None
                        for line in tests.decode().split("\n"):
                            i = line.find("#")
                            if i >= 0:
                                line = line[:i]
                            if not line:
                                continue
                            if line[0] != " ":
                                base = line.strip()
                            else:
                                assert base is not None
                                assert line[1] == " "
                                test = base + line.strip()
                                cmdline = [
                                    binary,
                                    "--gtest_filter=%s" % test,
                                ] + target["args"]
                                out.append(
                                    self.config.job_spec(
                                        cmdline,
                                        shortname="%s %s"
                                        % (" ".join(cmdline), shortname_ext),
                                        cpu_cost=cpu_cost,
                                        timeout_seconds=target.get(
                                            "timeout_seconds",
                                            _DEFAULT_TIMEOUT_SECONDS,
                                        )
                                        * timeout_scaling,
                                        environ=env,
                                    )
                                )
                    else:
                        cmdline = [binary] + target["args"]
                        shortname = target.get(
                            "shortname",
                            " ".join(pipes.quote(arg) for arg in cmdline),
                        )
                        shortname += shortname_ext
                        out.append(
                            self.config.job_spec(
                                cmdline,
                                shortname=shortname,
                                cpu_cost=cpu_cost,
                                flaky=target.get("flaky", False),
                                timeout_seconds=target.get(
                                    "timeout_seconds", _DEFAULT_TIMEOUT_SECONDS
                                )
                                * timeout_scaling,
                                environ=env,
                            )
                        )
                elif self.args.regex == ".*" or self.platform == "windows":
                    print("\nWARNING: binary not found, skipping", binary)
        return sorted(out)

    def pre_build_steps(self):
        return []

    def build_steps(self):
        if self.platform == "windows":
            return [
                [
                    "tools\\run_tests\\helper_scripts\\build_cxx.bat",
                    "-DgRPC_BUILD_MSVC_MP_COUNT=%d" % self.args.jobs,
                ]
                + self._cmake_configure_extra_args
            ]
        else:
            return [
                ["tools/run_tests/helper_scripts/build_cxx.sh"]
                + self._cmake_configure_extra_args
            ]

    def build_steps_environ(self):
        """Extra environment variables set for pre_build_steps and build_steps jobs."""
        environ = {"GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX": self.lang_suffix}
        if self.platform == "windows":
            environ["GRPC_CMAKE_GENERATOR"] = self._cmake_generator_windows
            environ[
                "GRPC_CMAKE_ARCHITECTURE"
            ] = self._cmake_architecture_windows
            environ[
                "GRPC_BUILD_ACTIVATE_VS_TOOLS"
            ] = self._activate_vs_tools_windows
            environ[
                "GRPC_BUILD_VS_TOOLS_ARCHITECTURE"
            ] = self._vs_tools_architecture_windows
        return environ

    def post_tests_steps(self):
        if self.platform == "windows":
            return []
        else:
            return [["tools/run_tests/helper_scripts/post_tests_c.sh"]]

    def _clang_cmake_configure_extra_args(self, version_suffix=""):
        return [
            "-DCMAKE_C_COMPILER=clang%s" % version_suffix,
            "-DCMAKE_CXX_COMPILER=clang++%s" % version_suffix,
        ]

    def _compiler_options(self, use_docker, compiler):
        """Returns docker distro and cmake configure args to use for given compiler."""
        if not use_docker and not _is_use_docker_child():
            # if not running under docker, we cannot ensure the right compiler version will be used,
            # so we only allow the non-specific choices.
            _check_compiler(compiler, ["default", "cmake"])

        if compiler == "default" or compiler == "cmake":
            return ("debian11", [])
        elif compiler == "gcc7":
            return ("gcc_7", [])
        elif compiler == "gcc10.2":
            return ("debian11", [])
        elif compiler == "gcc10.2_openssl102":
            return (
                "debian11_openssl102",
                [
                    "-DgRPC_SSL_PROVIDER=package",
                ],
            )
        elif compiler == "gcc12":
            return ("gcc_12", ["-DCMAKE_CXX_STANDARD=20"])
        elif compiler == "gcc_musl":
            return ("alpine", [])
        elif compiler == "clang6":
            return ("clang_6", self._clang_cmake_configure_extra_args())
        elif compiler == "clang15":
            return ("clang_15", self._clang_cmake_configure_extra_args())
        else:
            raise Exception("Compiler %s not supported." % compiler)

    def dockerfile_dir(self):
        return "tools/dockerfile/test/cxx_%s_%s" % (
            self._docker_distro,
            _docker_arch_suffix(self.args.arch),
        )

    def __str__(self):
        return self.lang_suffix


# This tests Node on grpc/grpc-node and will become the standard for Node testing
class RemoteNodeLanguage(object):
    def __init__(self):
        self.platform = platform_string()

    def configure(self, config, args):
        self.config = config
        self.args = args
        # Note: electron ABI only depends on major and minor version, so that's all
        # we should specify in the compiler argument
        _check_compiler(
            self.args.compiler,
            [
                "default",
                "node0.12",
                "node4",
                "node5",
                "node6",
                "node7",
                "node8",
                "electron1.3",
                "electron1.6",
            ],
        )
        if self.args.compiler == "default":
            self.runtime = "node"
            self.node_version = "8"
        else:
            if self.args.compiler.startswith("electron"):
                self.runtime = "electron"
                self.node_version = self.args.compiler[8:]
            else:
                self.runtime = "node"
                # Take off the word "node"
                self.node_version = self.args.compiler[4:]

    # TODO: update with Windows/electron scripts when available for grpc/grpc-node
    def test_specs(self):
        if self.platform == "windows":
            return [
                self.config.job_spec(
                    ["tools\\run_tests\\helper_scripts\\run_node.bat"]
                )
            ]
        else:
            return [
                self.config.job_spec(
                    ["tools/run_tests/helper_scripts/run_grpc-node.sh"],
                    None,
                    environ=_FORCE_ENVIRON_FOR_WRAPPERS,
                )
            ]

    def pre_build_steps(self):
        return []

    def build_steps(self):
        return []

    def build_steps_environ(self):
        """Extra environment variables set for pre_build_steps and build_steps jobs."""
        return {}

    def post_tests_steps(self):
        return []

    def dockerfile_dir(self):
        return "tools/dockerfile/test/node_jessie_%s" % _docker_arch_suffix(
            self.args.arch
        )

    def __str__(self):
        return "grpc-node"


class Php7Language(object):
    def configure(self, config, args):
        self.config = config
        self.args = args
        _check_compiler(self.args.compiler, ["default"])

    def test_specs(self):
        return [
            self.config.job_spec(
                ["src/php/bin/run_tests.sh"],
                environ=_FORCE_ENVIRON_FOR_WRAPPERS,
            )
        ]

    def pre_build_steps(self):
        return []

    def build_steps(self):
        return [["tools/run_tests/helper_scripts/build_php.sh"]]

    def build_steps_environ(self):
        """Extra environment variables set for pre_build_steps and build_steps jobs."""
        return {}

    def post_tests_steps(self):
        return [["tools/run_tests/helper_scripts/post_tests_php.sh"]]

    def dockerfile_dir(self):
        return "tools/dockerfile/test/php7_debian11_%s" % _docker_arch_suffix(
            self.args.arch
        )

    def __str__(self):
        return "php7"


class PythonConfig(
    collections.namedtuple("PythonConfig", ["name", "build", "run"])
):
    """Tuple of commands (named s.t. 'what it says on the tin' applies)"""


class PythonLanguage(object):
    _TEST_SPECS_FILE = {
        "native": ["src/python/grpcio_tests/tests/tests.json"],
        "gevent": [
            "src/python/grpcio_tests/tests/tests.json",
            "src/python/grpcio_tests/tests_gevent/tests.json",
        ],
        "asyncio": ["src/python/grpcio_tests/tests_aio/tests.json"],
    }

    _TEST_COMMAND = {
        "native": "test_lite",
        "gevent": "test_gevent",
        "asyncio": "test_aio",
    }

    def configure(self, config, args):
        self.config = config
        self.args = args
        self.pythons = self._get_pythons(self.args)

    def test_specs(self):
        # load list of known test suites
        jobs = []
        for io_platform in self._TEST_SPECS_FILE:
            test_cases = []
            for tests_json_file_name in self._TEST_SPECS_FILE[io_platform]:
                with open(tests_json_file_name) as tests_json_file:
                    test_cases.extend(json.load(tests_json_file))

            environment = dict(_FORCE_ENVIRON_FOR_WRAPPERS)
            # TODO(https://github.com/grpc/grpc/issues/21401) Fork handlers is not
            # designed for non-native IO manager. It has a side-effect that
            # overrides threading settings in C-Core.
            if io_platform != "native":
                environment["GRPC_ENABLE_FORK_SUPPORT"] = "0"
            for python_config in self.pythons:
                jobs.extend(
                    [
                        self.config.job_spec(
                            python_config.run
                            + [self._TEST_COMMAND[io_platform]],
                            timeout_seconds=8 * 60,
                            environ=dict(
                                GRPC_PYTHON_TESTRUNNER_FILTER=str(test_case),
                                **environment,
                            ),
                            shortname="%s.%s.%s"
                            % (python_config.name, io_platform, test_case),
                        )
                        for test_case in test_cases
                    ]
                )
        return jobs

    def pre_build_steps(self):
        return []

    def build_steps(self):
        return [config.build for config in self.pythons]

    def build_steps_environ(self):
        """Extra environment variables set for pre_build_steps and build_steps jobs."""
        return {}

    def post_tests_steps(self):
        if self.config.build_config != "gcov":
            return []
        else:
            return [["tools/run_tests/helper_scripts/post_tests_python.sh"]]

    def dockerfile_dir(self):
        return "tools/dockerfile/test/python_%s_%s" % (
            self._python_docker_distro_name(),
            _docker_arch_suffix(self.args.arch),
        )

    def _python_docker_distro_name(self):
        """Choose the docker image to use based on python version."""
        if self.args.compiler == "python_alpine":
            return "alpine"
        else:
            return "debian11_default"

    def _get_pythons(self, args):
        """Get python runtimes to test with, based on current platform, architecture, compiler etc."""
        if args.iomgr_platform != "native":
            raise ValueError(
                "Python builds no longer differentiate IO Manager platforms,"
                ' please use "native"'
            )

        if args.arch == "x86":
            bits = "32"
        else:
            bits = "64"

        if os.name == "nt":
            shell = ["bash"]
            builder = [
                os.path.abspath(
                    "tools/run_tests/helper_scripts/build_python_msys2.sh"
                )
            ]
            builder_prefix_arguments = ["MINGW{}".format(bits)]
            venv_relative_python = ["Scripts/python.exe"]
            toolchain = ["mingw32"]
        else:
            shell = []
            builder = [
                os.path.abspath(
                    "tools/run_tests/helper_scripts/build_python.sh"
                )
            ]
            builder_prefix_arguments = []
            venv_relative_python = ["bin/python"]
            toolchain = ["unix"]

        runner = [
            os.path.abspath("tools/run_tests/helper_scripts/run_python.sh")
        ]

        config_vars = _PythonConfigVars(
            shell,
            builder,
            builder_prefix_arguments,
            venv_relative_python,
            toolchain,
            runner,
        )
        python37_config = _python_config_generator(
            name="py37",
            major="3",
            minor="7",
            bits=bits,
            config_vars=config_vars,
        )
        python38_config = _python_config_generator(
            name="py38",
            major="3",
            minor="8",
            bits=bits,
            config_vars=config_vars,
        )
        python39_config = _python_config_generator(
            name="py39",
            major="3",
            minor="9",
            bits=bits,
            config_vars=config_vars,
        )
        python310_config = _python_config_generator(
            name="py310",
            major="3",
            minor="10",
            bits=bits,
            config_vars=config_vars,
        )
        pypy27_config = _pypy_config_generator(
            name="pypy", major="2", config_vars=config_vars
        )
        pypy32_config = _pypy_config_generator(
            name="pypy3", major="3", config_vars=config_vars
        )

        if args.compiler == "default":
            if os.name == "nt":
                return (python38_config,)
            elif os.uname()[0] == "Darwin":
                # NOTE(rbellevi): Testing takes significantly longer on
                # MacOS, so we restrict the number of interpreter versions
                # tested.
                return (python38_config,)
            elif platform.machine() == "aarch64":
                # Currently the python_debian11_default_arm64 docker image
                # only has python3.9 installed (and that seems sufficient
                # for arm64 testing)
                return (python39_config,)
            else:
                return (
                    python37_config,
                    python38_config,
                )
        elif args.compiler == "python3.7":
            return (python37_config,)
        elif args.compiler == "python3.8":
            return (python38_config,)
        elif args.compiler == "python3.9":
            return (python39_config,)
        elif args.compiler == "python3.10":
            return (python310_config,)
        elif args.compiler == "pypy":
            return (pypy27_config,)
        elif args.compiler == "pypy3":
            return (pypy32_config,)
        elif args.compiler == "python_alpine":
            return (python39_config,)
        elif args.compiler == "all_the_cpythons":
            return (
                python37_config,
                python38_config,
                python39_config,
                python310_config,
            )
        else:
            raise Exception("Compiler %s not supported." % args.compiler)

    def __str__(self):
        return "python"


class RubyLanguage(object):
    def configure(self, config, args):
        self.config = config
        self.args = args
        _check_compiler(self.args.compiler, ["default"])

    def test_specs(self):
        tests = [
            self.config.job_spec(
                ["tools/run_tests/helper_scripts/run_ruby.sh"],
                timeout_seconds=10 * 60,
                environ=_FORCE_ENVIRON_FOR_WRAPPERS,
            )
        ]
        # TODO(apolcyn): re-enable the following tests after
        # https://bugs.ruby-lang.org/issues/15499 is fixed:
        # They previously worked on ruby 2.5 but needed to be disabled
        # after dropping support for ruby 2.5:
        #   - src/ruby/end2end/channel_state_test.rb
        #   - src/ruby/end2end/sig_int_during_channel_watch_test.rb
        # TODO(apolcyn): the following test is skipped because it sometimes
        # hits "Bus Error" crashes while requiring the grpc/ruby C-extension.
        # This crashes have been unreproducible outside of CI. Also see
        # b/266212253.
        #   - src/ruby/end2end/grpc_class_init_test.rb
        for test in [
            "src/ruby/end2end/fork_test.rb",
            "src/ruby/end2end/simple_fork_test.rb",
            "src/ruby/end2end/secure_fork_test.rb",
            "src/ruby/end2end/bad_usage_fork_test.rb",
            "src/ruby/end2end/sig_handling_test.rb",
            "src/ruby/end2end/channel_closing_test.rb",
            "src/ruby/end2end/killed_client_thread_test.rb",
            "src/ruby/end2end/forking_client_test.rb",
            "src/ruby/end2end/multiple_killed_watching_threads_test.rb",
            "src/ruby/end2end/load_grpc_with_gc_stress_test.rb",
            "src/ruby/end2end/client_memory_usage_test.rb",
            "src/ruby/end2end/package_with_underscore_test.rb",
            "src/ruby/end2end/graceful_sig_handling_test.rb",
            "src/ruby/end2end/graceful_sig_stop_test.rb",
            "src/ruby/end2end/errors_load_before_grpc_lib_test.rb",
            "src/ruby/end2end/logger_load_before_grpc_lib_test.rb",
            "src/ruby/end2end/status_codes_load_before_grpc_lib_test.rb",
            "src/ruby/end2end/call_credentials_timeout_test.rb",
            "src/ruby/end2end/call_credentials_returning_bad_metadata_doesnt_kill_background_thread_test.rb",
        ]:
            if test in [
                "src/ruby/end2end/fork_test.rb",
                "src/ruby/end2end/simple_fork_test.rb",
                "src/ruby/end2end/secure_fork_test.rb",
                "src/ruby/end2end/bad_usage_fork_test.rb",
            ]:
                if platform_string() == "mac":
                    # Skip fork tests on mac, it's only supported on linux.
                    continue
                if self.config.build_config == "dbg":
                    # There's a known issue with dbg builds that breaks fork
                    # support: https://github.com/grpc/grpc/issues/31885.
                    # TODO(apolcyn): unskip these tests on dbg builds after we
                    # migrate to event engine and hence fix that issue.
                    continue
            tests.append(
                self.config.job_spec(
                    ["ruby", test],
                    shortname=test,
                    timeout_seconds=20 * 60,
                    environ=_FORCE_ENVIRON_FOR_WRAPPERS,
                )
            )
        return tests

    def pre_build_steps(self):
        return [["tools/run_tests/helper_scripts/pre_build_ruby.sh"]]

    def build_steps(self):
        return [["tools/run_tests/helper_scripts/build_ruby.sh"]]

    def build_steps_environ(self):
        """Extra environment variables set for pre_build_steps and build_steps jobs."""
        return {}

    def post_tests_steps(self):
        return [["tools/run_tests/helper_scripts/post_tests_ruby.sh"]]

    def dockerfile_dir(self):
        return "tools/dockerfile/test/ruby_debian11_%s" % _docker_arch_suffix(
            self.args.arch
        )

    def __str__(self):
        return "ruby"


class CSharpLanguage(object):
    def __init__(self):
        self.platform = platform_string()

    def configure(self, config, args):
        self.config = config
        self.args = args
        _check_compiler(self.args.compiler, ["default", "coreclr", "mono"])
        if self.args.compiler == "default":
            # test both runtimes by default
            self.test_runtimes = ["coreclr", "mono"]
        else:
            # only test the specified runtime
            self.test_runtimes = [self.args.compiler]

        if self.platform == "windows":
            _check_arch(self.args.arch, ["default"])
            self._cmake_arch_option = "x64"
        else:
            self._docker_distro = "debian11"

    def test_specs(self):
        with open("src/csharp/tests.json") as f:
            tests_by_assembly = json.load(f)

        msbuild_config = _MSBUILD_CONFIG[self.config.build_config]
        nunit_args = ["--labels=All", "--noresult", "--workers=1"]

        specs = []
        for test_runtime in self.test_runtimes:
            if test_runtime == "coreclr":
                assembly_extension = ".dll"
                assembly_subdir = "bin/%s/netcoreapp3.1" % msbuild_config
                runtime_cmd = ["dotnet", "exec"]
            elif test_runtime == "mono":
                assembly_extension = ".exe"
                assembly_subdir = "bin/%s/net45" % msbuild_config
                if self.platform == "windows":
                    runtime_cmd = []
                elif self.platform == "mac":
                    # mono before version 5.2 on MacOS defaults to 32bit runtime
                    runtime_cmd = ["mono", "--arch=64"]
                else:
                    runtime_cmd = ["mono"]
            else:
                raise Exception('Illegal runtime "%s" was specified.')

            for assembly in six.iterkeys(tests_by_assembly):
                assembly_file = "src/csharp/%s/%s/%s%s" % (
                    assembly,
                    assembly_subdir,
                    assembly,
                    assembly_extension,
                )

                # normally, run each test as a separate process
                for test in tests_by_assembly[assembly]:
                    cmdline = (
                        runtime_cmd
                        + [assembly_file, "--test=%s" % test]
                        + nunit_args
                    )
                    specs.append(
                        self.config.job_spec(
                            cmdline,
                            shortname="csharp.%s.%s" % (test_runtime, test),
                            environ=_FORCE_ENVIRON_FOR_WRAPPERS,
                        )
                    )
        return specs

    def pre_build_steps(self):
        if self.platform == "windows":
            return [["tools\\run_tests\\helper_scripts\\pre_build_csharp.bat"]]
        else:
            return [["tools/run_tests/helper_scripts/pre_build_csharp.sh"]]

    def build_steps(self):
        if self.platform == "windows":
            return [["tools\\run_tests\\helper_scripts\\build_csharp.bat"]]
        else:
            return [["tools/run_tests/helper_scripts/build_csharp.sh"]]

    def build_steps_environ(self):
        """Extra environment variables set for pre_build_steps and build_steps jobs."""
        if self.platform == "windows":
            return {"ARCHITECTURE": self._cmake_arch_option}
        else:
            return {}

    def post_tests_steps(self):
        if self.platform == "windows":
            return [["tools\\run_tests\\helper_scripts\\post_tests_csharp.bat"]]
        else:
            return [["tools/run_tests/helper_scripts/post_tests_csharp.sh"]]

    def dockerfile_dir(self):
        return "tools/dockerfile/test/csharp_%s_%s" % (
            self._docker_distro,
            _docker_arch_suffix(self.args.arch),
        )

    def __str__(self):
        return "csharp"


class ObjCLanguage(object):
    def configure(self, config, args):
        self.config = config
        self.args = args
        _check_compiler(self.args.compiler, ["default"])

    def test_specs(self):
        out = []
        out.append(
            self.config.job_spec(
                ["src/objective-c/tests/build_one_example.sh"],
                timeout_seconds=20 * 60,
                shortname="ios-buildtest-example-sample",
                cpu_cost=1e6,
                environ={
                    "SCHEME": "Sample",
                    "EXAMPLE_PATH": "src/objective-c/examples/Sample",
                },
            )
        )
        # TODO(jtattermusch): Create bazel target for the sample and remove the test task from here.
        out.append(
            self.config.job_spec(
                ["src/objective-c/tests/build_one_example.sh"],
                timeout_seconds=20 * 60,
                shortname="ios-buildtest-example-switftsample",
                cpu_cost=1e6,
                environ={
                    "SCHEME": "SwiftSample",
                    "EXAMPLE_PATH": "src/objective-c/examples/SwiftSample",
                },
            )
        )
        # Disabled due to #20258
        # TODO (mxyan): Reenable this test when #20258 is resolved.
        # out.append(
        #     self.config.job_spec(
        #         ['src/objective-c/tests/build_one_example_bazel.sh'],
        #         timeout_seconds=20 * 60,
        #         shortname='ios-buildtest-example-watchOS-sample',
        #         cpu_cost=1e6,
        #         environ={
        #             'SCHEME': 'watchOS-sample-WatchKit-App',
        #             'EXAMPLE_PATH': 'src/objective-c/examples/watchOS-sample',
        #             'FRAMEWORKS': 'NO'
        #         }))

        # TODO(jtattermusch): move the test out of the test/core/iomgr/CFStreamTests directory?
        # How does one add the cfstream dependency in bazel?
        out.append(
            self.config.job_spec(
                ["test/core/iomgr/ios/CFStreamTests/build_and_run_tests.sh"],
                timeout_seconds=60 * 60,
                shortname="ios-test-cfstream-tests",
                cpu_cost=1e6,
                environ=_FORCE_ENVIRON_FOR_WRAPPERS,
            )
        )
        return sorted(out)

    def pre_build_steps(self):
        return []

    def build_steps(self):
        return []

    def build_steps_environ(self):
        """Extra environment variables set for pre_build_steps and build_steps jobs."""
        return {}

    def post_tests_steps(self):
        return []

    def dockerfile_dir(self):
        return None

    def __str__(self):
        return "objc"


class Sanity(object):
    def __init__(self, config_file):
        self.config_file = config_file

    def configure(self, config, args):
        self.config = config
        self.args = args
        _check_compiler(self.args.compiler, ["default"])

    def test_specs(self):
        import yaml

        with open("tools/run_tests/sanity/%s" % self.config_file, "r") as f:
            environ = {"TEST": "true"}
            if _is_use_docker_child():
                environ["CLANG_FORMAT_SKIP_DOCKER"] = "true"
                environ["CLANG_TIDY_SKIP_DOCKER"] = "true"
                environ["IWYU_SKIP_DOCKER"] = "true"
                # sanity tests run tools/bazel wrapper concurrently
                # and that can result in a download/run race in the wrapper.
                # under docker we already have the right version of bazel
                # so we can just disable the wrapper.
                environ["DISABLE_BAZEL_WRAPPER"] = "true"
            return [
                self.config.job_spec(
                    cmd["script"].split(),
                    timeout_seconds=45 * 60,
                    environ=environ,
                    cpu_cost=cmd.get("cpu_cost", 1),
                )
                for cmd in yaml.safe_load(f)
            ]

    def pre_build_steps(self):
        return []

    def build_steps(self):
        return []

    def build_steps_environ(self):
        """Extra environment variables set for pre_build_steps and build_steps jobs."""
        return {}

    def post_tests_steps(self):
        return []

    def dockerfile_dir(self):
        return "tools/dockerfile/test/sanity"

    def __str__(self):
        return "sanity"


# different configurations we can run under
with open("tools/run_tests/generated/configs.json") as f:
    _CONFIGS = dict(
        (cfg["config"], Config(**cfg)) for cfg in ast.literal_eval(f.read())
    )

_LANGUAGES = {
    "c++": CLanguage("cxx", "c++"),
    "c": CLanguage("c", "c"),
    "grpc-node": RemoteNodeLanguage(),
    "php7": Php7Language(),
    "python": PythonLanguage(),
    "ruby": RubyLanguage(),
    "csharp": CSharpLanguage(),
    "objc": ObjCLanguage(),
    "sanity": Sanity("sanity_tests.yaml"),
    "clang-tidy": Sanity("clang_tidy_tests.yaml"),
    "iwyu": Sanity("iwyu_tests.yaml"),
}

_MSBUILD_CONFIG = {
    "dbg": "Debug",
    "opt": "Release",
    "gcov": "Debug",
}


def _build_step_environ(cfg, extra_env={}):
    """Environment variables set for each build step."""
    environ = {"CONFIG": cfg, "GRPC_RUN_TESTS_JOBS": str(args.jobs)}
    msbuild_cfg = _MSBUILD_CONFIG.get(cfg)
    if msbuild_cfg:
        environ["MSBUILD_CONFIG"] = msbuild_cfg
    environ.update(extra_env)
    return environ


def _windows_arch_option(arch):
    """Returns msbuild cmdline option for selected architecture."""
    if arch == "default" or arch == "x86":
        return "/p:Platform=Win32"
    elif arch == "x64":
        return "/p:Platform=x64"
    else:
        print("Architecture %s not supported." % arch)
        sys.exit(1)


def _check_arch_option(arch):
    """Checks that architecture option is valid."""
    if platform_string() == "windows":
        _windows_arch_option(arch)
    elif platform_string() == "linux":
        # On linux, we need to be running under docker with the right architecture.
        runtime_machine = platform.machine()
        runtime_arch = platform.architecture()[0]
        if arch == "default":
            return
        elif (
            runtime_machine == "x86_64"
            and runtime_arch == "64bit"
            and arch == "x64"
        ):
            return
        elif (
            runtime_machine == "x86_64"
            and runtime_arch == "32bit"
            and arch == "x86"
        ):
            return
        elif (
            runtime_machine == "aarch64"
            and runtime_arch == "64bit"
            and arch == "arm64"
        ):
            return
        else:
            print(
                "Architecture %s does not match current runtime architecture."
                % arch
            )
            sys.exit(1)
    else:
        if args.arch != "default":
            print(
                "Architecture %s not supported on current platform." % args.arch
            )
            sys.exit(1)


def _docker_arch_suffix(arch):
    """Returns suffix to dockerfile dir to use."""
    if arch == "default" or arch == "x64":
        return "x64"
    elif arch == "x86":
        return "x86"
    elif arch == "arm64":
        return "arm64"
    else:
        print("Architecture %s not supported with current settings." % arch)
        sys.exit(1)


def runs_per_test_type(arg_str):
    """Auxiliary function to parse the "runs_per_test" flag.

    Returns:
        A positive integer or 0, the latter indicating an infinite number of
        runs.

    Raises:
        argparse.ArgumentTypeError: Upon invalid input.
    """
    if arg_str == "inf":
        return 0
    try:
        n = int(arg_str)
        if n <= 0:
            raise ValueError
        return n
    except:
        msg = "'{}' is not a positive integer or 'inf'".format(arg_str)
        raise argparse.ArgumentTypeError(msg)


def percent_type(arg_str):
    pct = float(arg_str)
    if pct > 100 or pct < 0:
        raise argparse.ArgumentTypeError(
            "'%f' is not a valid percentage in the [0, 100] range" % pct
        )
    return pct


# This is math.isclose in python >= 3.5
def isclose(a, b, rel_tol=1e-09, abs_tol=0.0):
    return abs(a - b) <= max(rel_tol * max(abs(a), abs(b)), abs_tol)


def _shut_down_legacy_server(legacy_server_port):
    """Shut down legacy version of port server."""
    try:
        version = int(
            urllib.request.urlopen(
                "http://localhost:%d/version_number" % legacy_server_port,
                timeout=10,
            ).read()
        )
    except:
        pass
    else:
        urllib.request.urlopen(
            "http://localhost:%d/quitquitquit" % legacy_server_port
        ).read()


def _calculate_num_runs_failures(list_of_results):
    """Calculate number of runs and failures for a particular test.

    Args:
      list_of_results: (List) of JobResult object.
    Returns:
      A tuple of total number of runs and failures.
    """
    num_runs = len(list_of_results)  # By default, there is 1 run per JobResult.
    num_failures = 0
    for jobresult in list_of_results:
        if jobresult.retries > 0:
            num_runs += jobresult.retries
        if jobresult.num_failures > 0:
            num_failures += jobresult.num_failures
    return num_runs, num_failures


class BuildAndRunError(object):
    """Represents error type in _build_and_run."""

    BUILD = object()
    TEST = object()
    POST_TEST = object()


# returns a list of things that failed (or an empty list on success)
def _build_and_run(
    check_cancelled, newline_on_success, xml_report=None, build_only=False
):
    """Do one pass of building & running tests."""
    # build latest sequentially
    num_failures, resultset = jobset.run(
        build_steps,
        maxjobs=1,
        stop_on_failure=True,
        newline_on_success=newline_on_success,
        travis=args.travis,
    )
    if num_failures:
        return [BuildAndRunError.BUILD]

    if build_only:
        if xml_report:
            report_utils.render_junit_xml_report(
                resultset, xml_report, suite_name=args.report_suite_name
            )
        return []

    # start antagonists
    antagonists = [
        subprocess.Popen(["tools/run_tests/python_utils/antagonist.py"])
        for _ in range(0, args.antagonists)
    ]
    start_port_server.start_port_server()
    resultset = None
    num_test_failures = 0
    try:
        infinite_runs = runs_per_test == 0
        one_run = set(
            spec
            for language in languages
            for spec in language.test_specs()
            if (
                re.search(args.regex, spec.shortname)
                and (
                    args.regex_exclude == ""
                    or not re.search(args.regex_exclude, spec.shortname)
                )
            )
        )
        # When running on travis, we want out test runs to be as similar as possible
        # for reproducibility purposes.
        if args.travis and args.max_time <= 0:
            massaged_one_run = sorted(one_run, key=lambda x: x.cpu_cost)
        else:
            # whereas otherwise, we want to shuffle things up to give all tests a
            # chance to run.
            massaged_one_run = list(
                one_run
            )  # random.sample needs an indexable seq.
            num_jobs = len(massaged_one_run)
            # for a random sample, get as many as indicated by the 'sample_percent'
            # argument. By default this arg is 100, resulting in a shuffle of all
            # jobs.
            sample_size = int(num_jobs * args.sample_percent / 100.0)
            massaged_one_run = random.sample(massaged_one_run, sample_size)
            if not isclose(args.sample_percent, 100.0):
                assert (
                    args.runs_per_test == 1
                ), "Can't do sampling (-p) over multiple runs (-n)."
                print(
                    "Running %d tests out of %d (~%d%%)"
                    % (sample_size, num_jobs, args.sample_percent)
                )
        if infinite_runs:
            assert (
                len(massaged_one_run) > 0
            ), "Must have at least one test for a -n inf run"
        runs_sequence = (
            itertools.repeat(massaged_one_run)
            if infinite_runs
            else itertools.repeat(massaged_one_run, runs_per_test)
        )
        all_runs = itertools.chain.from_iterable(runs_sequence)

        if args.quiet_success:
            jobset.message(
                "START",
                "Running tests quietly, only failing tests will be reported",
                do_newline=True,
            )
        num_test_failures, resultset = jobset.run(
            all_runs,
            check_cancelled,
            newline_on_success=newline_on_success,
            travis=args.travis,
            maxjobs=args.jobs,
            maxjobs_cpu_agnostic=max_parallel_tests_for_current_platform(),
            stop_on_failure=args.stop_on_failure,
            quiet_success=args.quiet_success,
            max_time=args.max_time,
        )
        if resultset:
            for k, v in sorted(resultset.items()):
                num_runs, num_failures = _calculate_num_runs_failures(v)
                if num_failures > 0:
                    if num_failures == num_runs:  # what about infinite_runs???
                        jobset.message("FAILED", k, do_newline=True)
                    else:
                        jobset.message(
                            "FLAKE",
                            "%s [%d/%d runs flaked]"
                            % (k, num_failures, num_runs),
                            do_newline=True,
                        )
    finally:
        for antagonist in antagonists:
            antagonist.kill()
        if args.bq_result_table and resultset:
            upload_extra_fields = {
                "compiler": args.compiler,
                "config": args.config,
                "iomgr_platform": args.iomgr_platform,
                "language": args.language[
                    0
                ],  # args.language is a list but will always have one element when uploading to BQ is enabled.
                "platform": platform_string(),
            }
            try:
                upload_results_to_bq(
                    resultset, args.bq_result_table, upload_extra_fields
                )
            except NameError as e:
                logging.warning(
                    e
                )  # It's fine to ignore since this is not critical
        if xml_report and resultset:
            report_utils.render_junit_xml_report(
                resultset,
                xml_report,
                suite_name=args.report_suite_name,
                multi_target=args.report_multi_target,
            )

    number_failures, _ = jobset.run(
        post_tests_steps,
        maxjobs=1,
        stop_on_failure=False,
        newline_on_success=newline_on_success,
        travis=args.travis,
    )

    out = []
    if number_failures:
        out.append(BuildAndRunError.POST_TEST)
    if num_test_failures:
        out.append(BuildAndRunError.TEST)

    return out


# parse command line
argp = argparse.ArgumentParser(description="Run grpc tests.")
argp.add_argument(
    "-c", "--config", choices=sorted(_CONFIGS.keys()), default="opt"
)
argp.add_argument(
    "-n",
    "--runs_per_test",
    default=1,
    type=runs_per_test_type,
    help=(
        'A positive integer or "inf". If "inf", all tests will run in an '
        'infinite loop. Especially useful in combination with "-f"'
    ),
)
argp.add_argument("-r", "--regex", default=".*", type=str)
argp.add_argument("--regex_exclude", default="", type=str)
argp.add_argument("-j", "--jobs", default=multiprocessing.cpu_count(), type=int)
argp.add_argument("-s", "--slowdown", default=1.0, type=float)
argp.add_argument(
    "-p",
    "--sample_percent",
    default=100.0,
    type=percent_type,
    help="Run a random sample with that percentage of tests",
)
argp.add_argument(
    "-t",
    "--travis",
    default=False,
    action="store_const",
    const=True,
    help=(
        "When set, indicates that the script is running on CI (= not locally)."
    ),
)
argp.add_argument(
    "--newline_on_success", default=False, action="store_const", const=True
)
argp.add_argument(
    "-l",
    "--language",
    choices=sorted(_LANGUAGES.keys()),
    nargs="+",
    required=True,
)
argp.add_argument(
    "-S", "--stop_on_failure", default=False, action="store_const", const=True
)
argp.add_argument(
    "--use_docker",
    default=False,
    action="store_const",
    const=True,
    help="Run all the tests under docker. That provides "
    + "additional isolation and prevents the need to install "
    + "language specific prerequisites. Only available on Linux.",
)
argp.add_argument(
    "--allow_flakes",
    default=False,
    action="store_const",
    const=True,
    help=(
        "Allow flaky tests to show as passing (re-runs failed tests up to five"
        " times)"
    ),
)
argp.add_argument(
    "--arch",
    choices=["default", "x86", "x64", "arm64"],
    default="default",
    help=(
        'Selects architecture to target. For some platforms "default" is the'
        " only supported choice."
    ),
)
argp.add_argument(
    "--compiler",
    choices=[
        "default",
        "gcc7",
        "gcc10.2",
        "gcc10.2_openssl102",
        "gcc12",
        "gcc_musl",
        "clang6",
        "clang15",
        "python2.7",
        "python3.5",
        "python3.7",
        "python3.8",
        "python3.9",
        "pypy",
        "pypy3",
        "python_alpine",
        "all_the_cpythons",
        "electron1.3",
        "electron1.6",
        "coreclr",
        "cmake",
        "cmake_ninja_vs2019",
        "cmake_vs2019",
        "mono",
    ],
    default="default",
    help=(
        "Selects compiler to use. Allowed values depend on the platform and"
        " language."
    ),
)
argp.add_argument(
    "--iomgr_platform",
    choices=["native", "gevent", "asyncio"],
    default="native",
    help="Selects iomgr platform to build on",
)
argp.add_argument(
    "--build_only",
    default=False,
    action="store_const",
    const=True,
    help="Perform all the build steps but don't run any tests.",
)
argp.add_argument(
    "--measure_cpu_costs",
    default=False,
    action="store_const",
    const=True,
    help="Measure the cpu costs of tests",
)
argp.add_argument("-a", "--antagonists", default=0, type=int)
argp.add_argument(
    "-x",
    "--xml_report",
    default=None,
    type=str,
    help="Generates a JUnit-compatible XML report",
)
argp.add_argument(
    "--report_suite_name",
    default="tests",
    type=str,
    help="Test suite name to use in generated JUnit XML report",
)
argp.add_argument(
    "--report_multi_target",
    default=False,
    const=True,
    action="store_const",
    help=(
        "Generate separate XML report for each test job (Looks better in UIs)."
    ),
)
argp.add_argument(
    "--quiet_success",
    default=False,
    action="store_const",
    const=True,
    help=(
        "Don't print anything when a test passes. Passing tests also will not"
        " be reported in XML report. "
    )
    + "Useful when running many iterations of each test (argument -n).",
)
argp.add_argument(
    "--force_default_poller",
    default=False,
    action="store_const",
    const=True,
    help="Don't try to iterate over many polling strategies when they exist",
)
argp.add_argument(
    "--force_use_pollers",
    default=None,
    type=str,
    help=(
        "Only use the specified comma-delimited list of polling engines. "
        "Example: --force_use_pollers epoll1,poll "
        " (This flag has no effect if --force_default_poller flag is also used)"
    ),
)
argp.add_argument(
    "--max_time", default=-1, type=int, help="Maximum test runtime in seconds"
)
argp.add_argument(
    "--bq_result_table",
    default="",
    type=str,
    nargs="?",
    help="Upload test results to a specified BQ table.",
)
args = argp.parse_args()

flaky_tests = set()
shortname_to_cpu = {}

if args.force_default_poller:
    _POLLING_STRATEGIES = {}
elif args.force_use_pollers:
    _POLLING_STRATEGIES[platform_string()] = args.force_use_pollers.split(",")

jobset.measure_cpu_costs = args.measure_cpu_costs

# grab config
run_config = _CONFIGS[args.config]
build_config = run_config.build_config

# TODO(jtattermusch): is this setting applied/being used?
if args.travis:
    _FORCE_ENVIRON_FOR_WRAPPERS = {"GRPC_TRACE": "api"}

languages = set(_LANGUAGES[l] for l in args.language)
for l in languages:
    l.configure(run_config, args)

if len(languages) != 1:
    print("Building multiple languages simultaneously is not supported!")
    sys.exit(1)

# If --use_docker was used, respawn the run_tests.py script under a docker container
# instead of continuing.
if args.use_docker:
    if not args.travis:
        print("Seen --use_docker flag, will run tests under docker.")
        print("")
        print(
            "IMPORTANT: The changes you are testing need to be locally"
            " committed"
        )
        print(
            "because only the committed changes in the current branch will be"
        )
        print("copied to the docker environment.")
        time.sleep(5)

    dockerfile_dirs = set([l.dockerfile_dir() for l in languages])
    if len(dockerfile_dirs) > 1:
        print(
            "Languages to be tested require running under different docker "
            "images."
        )
        sys.exit(1)
    else:
        dockerfile_dir = next(iter(dockerfile_dirs))

    child_argv = [arg for arg in sys.argv if not arg == "--use_docker"]
    run_tests_cmd = "python3 tools/run_tests/run_tests.py %s" % " ".join(
        child_argv[1:]
    )

    env = os.environ.copy()
    env["DOCKERFILE_DIR"] = dockerfile_dir
    env["DOCKER_RUN_SCRIPT"] = "tools/run_tests/dockerize/docker_run.sh"
    env["DOCKER_RUN_SCRIPT_COMMAND"] = run_tests_cmd

    retcode = subprocess.call(
        "tools/run_tests/dockerize/build_and_run_docker.sh", shell=True, env=env
    )
    _print_debug_info_epilogue(dockerfile_dir=dockerfile_dir)
    sys.exit(retcode)

_check_arch_option(args.arch)

# collect pre-build steps (which get retried if they fail, e.g. to avoid
# flakes on downloading dependencies etc.)
build_steps = list(
    set(
        jobset.JobSpec(
            cmdline,
            environ=_build_step_environ(
                build_config, extra_env=l.build_steps_environ()
            ),
            timeout_seconds=_PRE_BUILD_STEP_TIMEOUT_SECONDS,
            flake_retries=2,
        )
        for l in languages
        for cmdline in l.pre_build_steps()
    )
)

# collect build steps
build_steps.extend(
    set(
        jobset.JobSpec(
            cmdline,
            environ=_build_step_environ(
                build_config, extra_env=l.build_steps_environ()
            ),
            timeout_seconds=None,
        )
        for l in languages
        for cmdline in l.build_steps()
    )
)

# collect post test steps
post_tests_steps = list(
    set(
        jobset.JobSpec(
            cmdline,
            environ=_build_step_environ(
                build_config, extra_env=l.build_steps_environ()
            ),
        )
        for l in languages
        for cmdline in l.post_tests_steps()
    )
)
runs_per_test = args.runs_per_test

errors = _build_and_run(
    check_cancelled=lambda: False,
    newline_on_success=args.newline_on_success,
    xml_report=args.xml_report,
    build_only=args.build_only,
)
if not errors:
    jobset.message("SUCCESS", "All tests passed", do_newline=True)
else:
    jobset.message("FAILED", "Some tests failed", do_newline=True)

if not _is_use_docker_child():
    # if --use_docker was used, the outer invocation of run_tests.py will
    # print the debug info instead.
    _print_debug_info_epilogue()

exit_code = 0
if BuildAndRunError.BUILD in errors:
    exit_code |= 1
if BuildAndRunError.TEST in errors:
    exit_code |= 2
if BuildAndRunError.POST_TEST in errors:
    exit_code |= 4
sys.exit(exit_code)
