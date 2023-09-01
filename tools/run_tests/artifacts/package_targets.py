#!/usr/bin/env python3
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
"""Definition of targets to build distribution packages."""

import os.path
import sys

sys.path.insert(0, os.path.abspath(".."))
import python_utils.jobset as jobset


def create_docker_jobspec(
    name,
    dockerfile_dir,
    shell_command,
    environ={},
    flake_retries=0,
    timeout_retries=0,
):
    """Creates jobspec for a task running under docker."""
    environ = environ.copy()

    docker_args = []
    for k, v in list(environ.items()):
        docker_args += ["-e", "%s=%s" % (k, v)]
    docker_env = {
        "DOCKERFILE_DIR": dockerfile_dir,
        "DOCKER_RUN_SCRIPT": "tools/run_tests/dockerize/docker_run.sh",
        "DOCKER_RUN_SCRIPT_COMMAND": shell_command,
        "OUTPUT_DIR": "artifacts",
    }
    jobspec = jobset.JobSpec(
        cmdline=["tools/run_tests/dockerize/build_and_run_docker.sh"]
        + docker_args,
        environ=docker_env,
        shortname="build_package.%s" % (name),
        timeout_seconds=30 * 60,
        flake_retries=flake_retries,
        timeout_retries=timeout_retries,
    )
    return jobspec


def create_jobspec(
    name,
    cmdline,
    environ=None,
    cwd=None,
    shell=False,
    flake_retries=0,
    timeout_retries=0,
    cpu_cost=1.0,
):
    """Creates jobspec."""
    jobspec = jobset.JobSpec(
        cmdline=cmdline,
        environ=environ,
        cwd=cwd,
        shortname="build_package.%s" % (name),
        timeout_seconds=10 * 60,
        flake_retries=flake_retries,
        timeout_retries=timeout_retries,
        cpu_cost=cpu_cost,
        shell=shell,
    )
    return jobspec


class CSharpPackage:
    """Builds C# packages."""

    def __init__(self, platform):
        self.platform = platform
        self.labels = ["package", "csharp", self.platform]
        self.name = "csharp_package_nuget_%s" % self.platform
        self.labels += ["nuget"]

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        del inner_jobs  # arg unused as there is little opportunity for parallelizing
        environ = {
            "GRPC_CSHARP_BUILD_SINGLE_PLATFORM_NUGET": os.getenv(
                "GRPC_CSHARP_BUILD_SINGLE_PLATFORM_NUGET", ""
            )
        }

        build_script = "src/csharp/build_nuget.sh"

        if self.platform == "linux":
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/test/csharp_debian11_x64",
                build_script,
                environ=environ,
            )
        else:
            repo_root = os.path.join(
                os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."
            )
            environ["EXTERNAL_GIT_ROOT"] = repo_root
            return create_jobspec(
                self.name, ["bash", build_script], environ=environ
            )

    def __str__(self):
        return self.name


class RubyPackage:
    """Collects ruby gems created in the artifact phase"""

    def __init__(self):
        self.name = "ruby_package"
        self.labels = ["package", "ruby", "linux"]

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        del inner_jobs  # arg unused as this step simply collects preexisting artifacts
        return create_docker_jobspec(
            self.name,
            "tools/dockerfile/grpc_artifact_centos6_x64",
            "tools/run_tests/artifacts/build_package_ruby.sh",
        )


class PythonPackage:
    """Collects python eggs and wheels created in the artifact phase"""

    def __init__(self):
        self.name = "python_package"
        self.labels = ["package", "python", "linux"]

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        del inner_jobs  # arg unused as this step simply collects preexisting artifacts
        # since the python package build does very little, we can use virtually
        # any image that has new-enough python, so reusing one of the images used
        # for artifact building seems natural.
        return create_docker_jobspec(
            self.name,
            "tools/dockerfile/grpc_artifact_python_manylinux2014_x64",
            "tools/run_tests/artifacts/build_package_python.sh",
            environ={"PYTHON": "/opt/python/cp39-cp39/bin/python"},
        )


class PHPPackage:
    """Copy PHP PECL package artifact"""

    def __init__(self):
        self.name = "php_package"
        self.labels = ["package", "php", "linux"]

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        del inner_jobs  # arg unused as this step simply collects preexisting artifacts
        return create_docker_jobspec(
            self.name,
            "tools/dockerfile/grpc_artifact_centos6_x64",
            "tools/run_tests/artifacts/build_package_php.sh",
        )


def targets():
    """Gets list of supported targets"""
    return [
        CSharpPackage("linux"),
        CSharpPackage("macos"),
        CSharpPackage("windows"),
        RubyPackage(),
        PythonPackage(),
        PHPPackage(),
    ]
