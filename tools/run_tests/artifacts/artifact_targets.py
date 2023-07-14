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
"""Definition of targets to build artifacts."""

import os.path
import random
import string
import sys

sys.path.insert(0, os.path.abspath(".."))
import python_utils.jobset as jobset

_LATEST_MANYLINUX = "manylinux2014"


def create_docker_jobspec(
    name,
    dockerfile_dir,
    shell_command,
    environ={},
    flake_retries=0,
    timeout_retries=0,
    timeout_seconds=30 * 60,
    extra_docker_args=None,
    verbose_success=False,
):
    """Creates jobspec for a task running under docker."""
    environ = environ.copy()
    environ["ARTIFACTS_OUT"] = "artifacts/%s" % name

    docker_args = []
    for k, v in list(environ.items()):
        docker_args += ["-e", "%s=%s" % (k, v)]
    docker_env = {
        "DOCKERFILE_DIR": dockerfile_dir,
        "DOCKER_RUN_SCRIPT": "tools/run_tests/dockerize/docker_run.sh",
        "DOCKER_RUN_SCRIPT_COMMAND": shell_command,
        "OUTPUT_DIR": "artifacts",
    }
    if extra_docker_args is not None:
        docker_env["EXTRA_DOCKER_ARGS"] = extra_docker_args
    jobspec = jobset.JobSpec(
        cmdline=["tools/run_tests/dockerize/build_and_run_docker.sh"]
        + docker_args,
        environ=docker_env,
        shortname="build_artifact.%s" % (name),
        timeout_seconds=timeout_seconds,
        flake_retries=flake_retries,
        timeout_retries=timeout_retries,
        verbose_success=verbose_success,
    )
    return jobspec


def create_jobspec(
    name,
    cmdline,
    environ={},
    shell=False,
    flake_retries=0,
    timeout_retries=0,
    timeout_seconds=30 * 60,
    use_workspace=False,
    cpu_cost=1.0,
    verbose_success=False,
):
    """Creates jobspec."""
    environ = environ.copy()
    if use_workspace:
        environ["WORKSPACE_NAME"] = "workspace_%s" % name
        environ["ARTIFACTS_OUT"] = os.path.join("..", "artifacts", name)
        cmdline = [
            "bash",
            "tools/run_tests/artifacts/run_in_workspace.sh",
        ] + cmdline
    else:
        environ["ARTIFACTS_OUT"] = os.path.join("artifacts", name)

    jobspec = jobset.JobSpec(
        cmdline=cmdline,
        environ=environ,
        shortname="build_artifact.%s" % (name),
        timeout_seconds=timeout_seconds,
        flake_retries=flake_retries,
        timeout_retries=timeout_retries,
        shell=shell,
        cpu_cost=cpu_cost,
        verbose_success=verbose_success,
    )
    return jobspec


_MACOS_COMPAT_FLAG = "-mmacosx-version-min=10.10"

_ARCH_FLAG_MAP = {"x86": "-m32", "x64": "-m64"}


class PythonArtifact:
    """Builds Python artifacts."""

    def __init__(self, platform, arch, py_version, presubmit=False):
        self.name = "python_%s_%s_%s" % (platform, arch, py_version)
        self.platform = platform
        self.arch = arch
        self.labels = ["artifact", "python", platform, arch, py_version]
        if presubmit:
            self.labels.append("presubmit")
        self.py_version = py_version
        if platform == _LATEST_MANYLINUX:
            self.labels.append("latest-manylinux")
        if "manylinux" in platform:
            self.labels.append("linux")
        if "linux_extra" in platform:
            # linux_extra wheels used to be built by a separate kokoro job.
            # Their build is now much faster, so they can be included
            # in the regular artifact build.
            self.labels.append("linux")
        if "musllinux" in platform:
            self.labels.append("linux")

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        environ = {}
        if inner_jobs is not None:
            # set number of parallel jobs when building native extension
            # building the native extension is the most time-consuming part of the build
            environ["GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS"] = str(inner_jobs)

        if self.platform == "macos":
            environ["ARCHFLAGS"] = "-arch arm64 -arch x86_64"
            environ["GRPC_UNIVERSAL2_REPAIR"] = "true"
            environ["GRPC_BUILD_WITH_BORING_SSL_ASM"] = "false"

        if self.platform == "linux_extra":
            # Crosscompilation build for armv7 (e.g. Raspberry Pi)
            environ["PYTHON"] = "/opt/python/{}/bin/python3".format(
                self.py_version
            )
            environ["PIP"] = "/opt/python/{}/bin/pip3".format(self.py_version)
            environ["GRPC_SKIP_PIP_CYTHON_UPGRADE"] = "TRUE"
            environ["GRPC_SKIP_TWINE_CHECK"] = "TRUE"
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/grpc_artifact_python_linux_{}".format(
                    self.arch
                ),
                "tools/run_tests/artifacts/build_artifact_python.sh",
                environ=environ,
                timeout_seconds=60 * 60,
            )
        elif "manylinux" in self.platform:
            if self.arch == "x86":
                environ["SETARCH_CMD"] = "linux32"
            # Inside the manylinux container, the python installations are located in
            # special places...
            environ["PYTHON"] = "/opt/python/{}/bin/python".format(
                self.py_version
            )
            environ["PIP"] = "/opt/python/{}/bin/pip".format(self.py_version)
            environ["GRPC_SKIP_PIP_CYTHON_UPGRADE"] = "TRUE"
            if self.arch == "aarch64":
                environ["GRPC_SKIP_TWINE_CHECK"] = "TRUE"
                # As we won't strip the binary with auditwheel (see below), strip
                # it at link time.
                environ["LDFLAGS"] = "-s"
            else:
                # only run auditwheel if we're not crosscompiling
                environ["GRPC_RUN_AUDITWHEEL_REPAIR"] = "TRUE"
                # only build the packages that depend on grpcio-tools
                # if we're not crosscompiling.
                # - they require protoc to run on current architecture
                # - they only have sdist packages anyway, so it's useless to build them again
                environ["GRPC_BUILD_GRPCIO_TOOLS_DEPENDENTS"] = "TRUE"
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/grpc_artifact_python_%s_%s"
                % (self.platform, self.arch),
                "tools/run_tests/artifacts/build_artifact_python.sh",
                environ=environ,
                timeout_seconds=60 * 60 * 2,
            )
        elif "musllinux" in self.platform:
            environ["PYTHON"] = "/opt/python/{}/bin/python".format(
                self.py_version
            )
            environ["PIP"] = "/opt/python/{}/bin/pip".format(self.py_version)
            environ["GRPC_SKIP_PIP_CYTHON_UPGRADE"] = "TRUE"
            environ["GRPC_RUN_AUDITWHEEL_REPAIR"] = "TRUE"
            environ["GRPC_PYTHON_BUILD_WITH_STATIC_LIBSTDCXX"] = "TRUE"
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/grpc_artifact_python_%s_%s"
                % (self.platform, self.arch),
                "tools/run_tests/artifacts/build_artifact_python.sh",
                environ=environ,
                timeout_seconds=60 * 60 * 2,
            )
        elif self.platform == "windows":
            environ["EXT_COMPILER"] = "msvc"
            # For some reason, the batch script %random% always runs with the same
            # seed.  We create a random temp-dir here
            dir = "".join(
                random.choice(string.ascii_uppercase) for _ in range(10)
            )
            return create_jobspec(
                self.name,
                [
                    "tools\\run_tests\\artifacts\\build_artifact_python.bat",
                    self.py_version,
                    "32" if self.arch == "x86" else "64",
                ],
                environ=environ,
                timeout_seconds=45 * 60,
                use_workspace=True,
            )
        else:
            environ["PYTHON"] = self.py_version
            environ["SKIP_PIP_INSTALL"] = "TRUE"
            return create_jobspec(
                self.name,
                ["tools/run_tests/artifacts/build_artifact_python.sh"],
                environ=environ,
                timeout_seconds=60 * 60 * 2,
                use_workspace=True,
            )

    def __str__(self):
        return self.name


class RubyArtifact:
    """Builds ruby native gem."""

    def __init__(self, platform, gem_platform, presubmit=False):
        self.name = "ruby_native_gem_%s_%s" % (platform, gem_platform)
        self.platform = platform
        self.gem_platform = gem_platform
        self.labels = ["artifact", "ruby", platform, gem_platform]
        if presubmit:
            self.labels.append("presubmit")

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        environ = {}
        if inner_jobs is not None:
            # set number of parallel jobs when building native extension
            environ["GRPC_RUBY_BUILD_PROCS"] = str(inner_jobs)
        # Ruby build uses docker internally and docker cannot be nested.
        # We are using a custom workspace instead.
        return create_jobspec(
            self.name,
            [
                "tools/run_tests/artifacts/build_artifact_ruby.sh",
                self.gem_platform,
            ],
            use_workspace=True,
            timeout_seconds=90 * 60,
            environ=environ,
        )


class PHPArtifact:
    """Builds PHP PECL package"""

    def __init__(self, platform, arch, presubmit=False):
        self.name = "php_pecl_package_{0}_{1}".format(platform, arch)
        self.platform = platform
        self.arch = arch
        self.labels = ["artifact", "php", platform, arch]
        if presubmit:
            self.labels.append("presubmit")

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        del inner_jobs  # arg unused as PHP artifact build is basically just packing an archive
        if self.platform == "linux":
            return create_docker_jobspec(
                self.name,
                "tools/dockerfile/test/php73_zts_debian11_{}".format(self.arch),
                "tools/run_tests/artifacts/build_artifact_php.sh",
            )
        else:
            return create_jobspec(
                self.name,
                ["tools/run_tests/artifacts/build_artifact_php.sh"],
                use_workspace=True,
            )


class ProtocArtifact:
    """Builds protoc and protoc-plugin artifacts"""

    def __init__(self, platform, arch, presubmit=False):
        self.name = "protoc_%s_%s" % (platform, arch)
        self.platform = platform
        self.arch = arch
        self.labels = ["artifact", "protoc", platform, arch]
        if presubmit:
            self.labels.append("presubmit")

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self, inner_jobs=None):
        environ = {}
        if inner_jobs is not None:
            # set number of parallel jobs when building protoc
            environ["GRPC_PROTOC_BUILD_COMPILER_JOBS"] = str(inner_jobs)

        if self.platform != "windows":
            environ["CXXFLAGS"] = ""
            environ["LDFLAGS"] = ""
            if self.platform == "linux":
                dockerfile_dir = (
                    "tools/dockerfile/grpc_artifact_centos6_{}".format(
                        self.arch
                    )
                )
                if self.arch == "aarch64":
                    # for aarch64, use a dockcross manylinux image that will
                    # give us both ready to use crosscompiler and sufficient backward compatibility
                    dockerfile_dir = (
                        "tools/dockerfile/grpc_artifact_protoc_aarch64"
                    )
                environ["LDFLAGS"] += " -static-libgcc -static-libstdc++ -s"
                return create_docker_jobspec(
                    self.name,
                    dockerfile_dir,
                    "tools/run_tests/artifacts/build_artifact_protoc.sh",
                    environ=environ,
                )
            else:
                environ["CXXFLAGS"] += (
                    " -std=c++14 -stdlib=libc++ %s" % _MACOS_COMPAT_FLAG
                )
                return create_jobspec(
                    self.name,
                    ["tools/run_tests/artifacts/build_artifact_protoc.sh"],
                    environ=environ,
                    timeout_seconds=60 * 60,
                    use_workspace=True,
                )
        else:
            vs_tools_architecture = (
                self.arch
            )  # architecture selector passed to vcvarsall.bat
            environ["ARCHITECTURE"] = vs_tools_architecture
            return create_jobspec(
                self.name,
                ["tools\\run_tests\\artifacts\\build_artifact_protoc.bat"],
                environ=environ,
                use_workspace=True,
            )

    def __str__(self):
        return self.name


def _reorder_targets_for_build_speed(targets):
    """Reorder targets to achieve optimal build speed"""
    # ruby artifact build builds multiple artifacts at once, so make sure
    # we start building ruby artifacts first, so that they don't end up
    # being a long tail once everything else finishes.
    return list(
        sorted(
            targets,
            key=lambda target: 0 if target.name.startswith("ruby_") else 1,
        )
    )


def targets():
    """Gets list of supported targets"""
    return _reorder_targets_for_build_speed(
        [
            ProtocArtifact("linux", "x64", presubmit=True),
            ProtocArtifact("linux", "x86", presubmit=True),
            ProtocArtifact("linux", "aarch64", presubmit=True),
            ProtocArtifact("macos", "x64", presubmit=True),
            ProtocArtifact("windows", "x64", presubmit=True),
            ProtocArtifact("windows", "x86", presubmit=True),
            PythonArtifact(
                "manylinux2014", "x64", "cp37-cp37m", presubmit=True
            ),
            PythonArtifact("manylinux2014", "x64", "cp38-cp38", presubmit=True),
            PythonArtifact("manylinux2014", "x64", "cp39-cp39"),
            PythonArtifact("manylinux2014", "x64", "cp310-cp310"),
            PythonArtifact(
                "manylinux2014", "x64", "cp311-cp311", presubmit=True
            ),
            PythonArtifact(
                "manylinux2014", "x86", "cp37-cp37m", presubmit=True
            ),
            PythonArtifact("manylinux2014", "x86", "cp38-cp38", presubmit=True),
            PythonArtifact("manylinux2014", "x86", "cp39-cp39"),
            PythonArtifact("manylinux2014", "x86", "cp310-cp310"),
            PythonArtifact(
                "manylinux2014", "x86", "cp311-cp311", presubmit=True
            ),
            PythonArtifact(
                "manylinux2014", "aarch64", "cp37-cp37m", presubmit=True
            ),
            PythonArtifact(
                "manylinux2014", "aarch64", "cp38-cp38", presubmit=True
            ),
            PythonArtifact("manylinux2014", "aarch64", "cp39-cp39"),
            PythonArtifact("manylinux2014", "aarch64", "cp310-cp310"),
            PythonArtifact("manylinux2014", "aarch64", "cp311-cp311"),
            PythonArtifact(
                "linux_extra", "armv7", "cp37-cp37m", presubmit=True
            ),
            PythonArtifact("linux_extra", "armv7", "cp38-cp38"),
            PythonArtifact("linux_extra", "armv7", "cp39-cp39"),
            PythonArtifact("linux_extra", "armv7", "cp310-cp310"),
            PythonArtifact(
                "linux_extra", "armv7", "cp311-cp311", presubmit=True
            ),
            PythonArtifact("musllinux_1_1", "x64", "cp310-cp310"),
            PythonArtifact(
                "musllinux_1_1", "x64", "cp311-cp311", presubmit=True
            ),
            PythonArtifact(
                "musllinux_1_1", "x64", "cp37-cp37m", presubmit=True
            ),
            PythonArtifact("musllinux_1_1", "x64", "cp38-cp38"),
            PythonArtifact("musllinux_1_1", "x64", "cp39-cp39"),
            PythonArtifact("musllinux_1_1", "x86", "cp310-cp310"),
            PythonArtifact(
                "musllinux_1_1", "x86", "cp311-cp311", presubmit=True
            ),
            PythonArtifact(
                "musllinux_1_1", "x86", "cp37-cp37m", presubmit=True
            ),
            PythonArtifact("musllinux_1_1", "x86", "cp38-cp38"),
            PythonArtifact("musllinux_1_1", "x86", "cp39-cp39"),
            PythonArtifact("macos", "x64", "python3.7", presubmit=True),
            PythonArtifact("macos", "x64", "python3.8"),
            PythonArtifact("macos", "x64", "python3.9"),
            PythonArtifact("macos", "x64", "python3.10", presubmit=True),
            PythonArtifact("macos", "x64", "python3.11", presubmit=True),
            PythonArtifact("windows", "x86", "Python37_32bit", presubmit=True),
            PythonArtifact("windows", "x86", "Python38_32bit"),
            PythonArtifact("windows", "x86", "Python39_32bit"),
            PythonArtifact("windows", "x86", "Python310_32bit"),
            PythonArtifact("windows", "x86", "Python311_32bit", presubmit=True),
            PythonArtifact("windows", "x64", "Python37", presubmit=True),
            PythonArtifact("windows", "x64", "Python38"),
            PythonArtifact("windows", "x64", "Python39"),
            PythonArtifact("windows", "x64", "Python310"),
            PythonArtifact("windows", "x64", "Python311", presubmit=True),
            RubyArtifact("linux", "x86-mingw32", presubmit=True),
            RubyArtifact("linux", "x64-mingw32", presubmit=True),
            RubyArtifact("linux", "x64-mingw-ucrt", presubmit=True),
            RubyArtifact("linux", "x86_64-linux", presubmit=True),
            RubyArtifact("linux", "x86-linux", presubmit=True),
            RubyArtifact("linux", "aarch64-linux", presubmit=True),
            RubyArtifact("linux", "x86_64-darwin", presubmit=True),
            RubyArtifact("linux", "arm64-darwin", presubmit=True),
            PHPArtifact("linux", "x64", presubmit=True),
            PHPArtifact("macos", "x64", presubmit=True),
        ]
    )
