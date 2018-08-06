#!/usr/bin/env python
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

sys.path.insert(0, os.path.abspath('..'))
import python_utils.jobset as jobset


def create_docker_jobspec(name,
                          dockerfile_dir,
                          shell_command,
                          environ={},
                          flake_retries=0,
                          timeout_retries=0,
                          timeout_seconds=30 * 60,
                          docker_base_image=None,
                          extra_docker_args=None,
                          verbose_success=False):
    """Creates jobspec for a task running under docker."""
    environ = environ.copy()
    environ['RUN_COMMAND'] = shell_command
    environ['ARTIFACTS_OUT'] = 'artifacts/%s' % name

    docker_args = []
    for k, v in environ.items():
        docker_args += ['-e', '%s=%s' % (k, v)]
    docker_env = {
        'DOCKERFILE_DIR': dockerfile_dir,
        'DOCKER_RUN_SCRIPT': 'tools/run_tests/dockerize/docker_run.sh',
        'OUTPUT_DIR': 'artifacts'
    }

    if docker_base_image is not None:
        docker_env['DOCKER_BASE_IMAGE'] = docker_base_image
    if extra_docker_args is not None:
        docker_env['EXTRA_DOCKER_ARGS'] = extra_docker_args
    jobspec = jobset.JobSpec(
        cmdline=['tools/run_tests/dockerize/build_and_run_docker.sh'] +
        docker_args,
        environ=docker_env,
        shortname='build_artifact.%s' % (name),
        timeout_seconds=timeout_seconds,
        flake_retries=flake_retries,
        timeout_retries=timeout_retries,
        verbose_success=verbose_success)
    return jobspec


def create_jobspec(name,
                   cmdline,
                   environ={},
                   shell=False,
                   flake_retries=0,
                   timeout_retries=0,
                   timeout_seconds=30 * 60,
                   use_workspace=False,
                   cpu_cost=1.0,
                   verbose_success=False):
    """Creates jobspec."""
    environ = environ.copy()
    if use_workspace:
        environ['WORKSPACE_NAME'] = 'workspace_%s' % name
        environ['ARTIFACTS_OUT'] = os.path.join('..', 'artifacts', name)
        cmdline = ['bash', 'tools/run_tests/artifacts/run_in_workspace.sh'
                  ] + cmdline
    else:
        environ['ARTIFACTS_OUT'] = os.path.join('artifacts', name)

    jobspec = jobset.JobSpec(
        cmdline=cmdline,
        environ=environ,
        shortname='build_artifact.%s' % (name),
        timeout_seconds=timeout_seconds,
        flake_retries=flake_retries,
        timeout_retries=timeout_retries,
        shell=shell,
        cpu_cost=cpu_cost,
        verbose_success=verbose_success)
    return jobspec


_MACOS_COMPAT_FLAG = '-mmacosx-version-min=10.7'

_ARCH_FLAG_MAP = {'x86': '-m32', 'x64': '-m64'}


class PythonArtifact:
    """Builds Python artifacts."""

    def __init__(self, platform, arch, py_version):
        self.name = 'python_%s_%s_%s' % (platform, arch, py_version)
        self.platform = platform
        self.arch = arch
        self.labels = ['artifact', 'python', platform, arch, py_version]
        self.py_version = py_version

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        environ = {}
        if self.platform == 'linux_extra':
            # Raspberry Pi build
            environ['PYTHON'] = '/usr/local/bin/python{}'.format(
                self.py_version)
            environ['PIP'] = '/usr/local/bin/pip{}'.format(self.py_version)
            # https://github.com/resin-io-projects/armv7hf-debian-qemu/issues/9
            # A QEMU bug causes submodule update to hang, so we copy directly
            environ['RELATIVE_COPY_PATH'] = '.'
            extra_args = ' --entrypoint=/usr/bin/qemu-arm-static '
            return create_docker_jobspec(
                self.name,
                'tools/dockerfile/grpc_artifact_linux_{}'.format(self.arch),
                'tools/run_tests/artifacts/build_artifact_python.sh',
                environ=environ,
                timeout_seconds=60 * 60 * 5,
                docker_base_image='quay.io/grpc/raspbian_{}'.format(self.arch),
                extra_docker_args=extra_args)
        elif self.platform == 'linux':
            if self.arch == 'x86':
                environ['SETARCH_CMD'] = 'linux32'
            # Inside the manylinux container, the python installations are located in
            # special places...
            environ['PYTHON'] = '/opt/python/{}/bin/python'.format(
                self.py_version)
            environ['PIP'] = '/opt/python/{}/bin/pip'.format(self.py_version)
            # Platform autodetection for the manylinux1 image breaks so we set the
            # defines ourselves.
            # TODO(atash) get better platform-detection support in core so we don't
            # need to do this manually...
            environ['CFLAGS'] = '-DGPR_MANYLINUX1=1'
            environ['GRPC_BUILD_GRPCIO_TOOLS_DEPENDENTS'] = 'TRUE'
            environ['GRPC_BUILD_MANYLINUX_WHEEL'] = 'TRUE'
            return create_docker_jobspec(
                self.name,
                'tools/dockerfile/grpc_artifact_python_manylinux_%s' %
                self.arch,
                'tools/run_tests/artifacts/build_artifact_python.sh',
                environ=environ,
                timeout_seconds=60 * 60,
                docker_base_image='quay.io/pypa/manylinux1_i686'
                if self.arch == 'x86' else 'quay.io/pypa/manylinux1_x86_64')
        elif self.platform == 'windows':
            if 'Python27' in self.py_version or 'Python34' in self.py_version:
                environ['EXT_COMPILER'] = 'mingw32'
            else:
                environ['EXT_COMPILER'] = 'msvc'
            # For some reason, the batch script %random% always runs with the same
            # seed.  We create a random temp-dir here
            dir = ''.join(
                random.choice(string.ascii_uppercase) for _ in range(10))
            return create_jobspec(
                self.name, [
                    'tools\\run_tests\\artifacts\\build_artifact_python.bat',
                    self.py_version, '32' if self.arch == 'x86' else '64'
                ],
                environ=environ,
                timeout_seconds=45 * 60,
                use_workspace=True)
        else:
            environ['PYTHON'] = self.py_version
            environ['SKIP_PIP_INSTALL'] = 'TRUE'
            return create_jobspec(
                self.name,
                ['tools/run_tests/artifacts/build_artifact_python.sh'],
                environ=environ,
                timeout_seconds=60 * 60 * 2,
                use_workspace=True)

    def __str__(self):
        return self.name


class RubyArtifact:
    """Builds ruby native gem."""

    def __init__(self, platform, arch):
        self.name = 'ruby_native_gem_%s_%s' % (platform, arch)
        self.platform = platform
        self.arch = arch
        self.labels = ['artifact', 'ruby', platform, arch]

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        # Ruby build uses docker internally and docker cannot be nested.
        # We are using a custom workspace instead.
        return create_jobspec(
            self.name, ['tools/run_tests/artifacts/build_artifact_ruby.sh'],
            use_workspace=True,
            timeout_seconds=45 * 60)


class CSharpExtArtifact:
    """Builds C# native extension library"""

    def __init__(self, platform, arch, arch_abi=None):
        self.name = 'csharp_ext_%s_%s' % (platform, arch)
        self.platform = platform
        self.arch = arch
        self.arch_abi = arch_abi
        self.labels = ['artifact', 'csharp', platform, arch]
        if arch_abi:
            self.name += '_%s' % arch_abi
            self.labels.append(arch_abi)

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        if self.arch == 'android':
            return create_docker_jobspec(
                self.name,
                'tools/dockerfile/grpc_artifact_android_ndk',
                'tools/run_tests/artifacts/build_artifact_csharp_android.sh',
                environ={
                    'ANDROID_ABI': self.arch_abi
                })
        elif self.arch == 'ios':
            return create_jobspec(
                self.name,
                ['tools/run_tests/artifacts/build_artifact_csharp_ios.sh'],
                use_workspace=True)
        elif self.platform == 'windows':
            cmake_arch_option = 'Win32' if self.arch == 'x86' else self.arch
            return create_jobspec(
                self.name, [
                    'tools\\run_tests\\artifacts\\build_artifact_csharp.bat',
                    cmake_arch_option
                ],
                use_workspace=True)
        else:
            environ = {
                'CONFIG': 'opt',
                'EMBED_OPENSSL': 'true',
                'EMBED_ZLIB': 'true',
                'CFLAGS': '-DGPR_BACKWARDS_COMPATIBILITY_MODE',
                'CXXFLAGS': '-DGPR_BACKWARDS_COMPATIBILITY_MODE',
                'LDFLAGS': ''
            }
            if self.platform == 'linux':
                return create_docker_jobspec(
                    self.name,
                    'tools/dockerfile/grpc_artifact_linux_%s' % self.arch,
                    'tools/run_tests/artifacts/build_artifact_csharp.sh',
                    environ=environ)
            else:
                archflag = _ARCH_FLAG_MAP[self.arch]
                environ['CFLAGS'] += ' %s %s' % (archflag, _MACOS_COMPAT_FLAG)
                environ['CXXFLAGS'] += ' %s %s' % (archflag, _MACOS_COMPAT_FLAG)
                environ['LDFLAGS'] += ' %s' % archflag
                return create_jobspec(
                    self.name,
                    ['tools/run_tests/artifacts/build_artifact_csharp.sh'],
                    environ=environ,
                    use_workspace=True)

    def __str__(self):
        return self.name


class PHPArtifact:
    """Builds PHP PECL package"""

    def __init__(self, platform, arch):
        self.name = 'php_pecl_package_{0}_{1}'.format(platform, arch)
        self.platform = platform
        self.arch = arch
        self.labels = ['artifact', 'php', platform, arch]

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        if self.platform == 'linux':
            return create_docker_jobspec(
                self.name, 'tools/dockerfile/grpc_artifact_linux_{}'.format(
                    self.arch),
                'tools/run_tests/artifacts/build_artifact_php.sh')
        else:
            return create_jobspec(
                self.name, ['tools/run_tests/artifacts/build_artifact_php.sh'],
                use_workspace=True)


class ProtocArtifact:
    """Builds protoc and protoc-plugin artifacts"""

    def __init__(self, platform, arch):
        self.name = 'protoc_%s_%s' % (platform, arch)
        self.platform = platform
        self.arch = arch
        self.labels = ['artifact', 'protoc', platform, arch]

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        if self.platform != 'windows':
            cxxflags = '-DNDEBUG %s' % _ARCH_FLAG_MAP[self.arch]
            ldflags = '%s' % _ARCH_FLAG_MAP[self.arch]
            if self.platform != 'macos':
                ldflags += '  -static-libgcc -static-libstdc++ -s'
            environ = {
                'CONFIG': 'opt',
                'CXXFLAGS': cxxflags,
                'LDFLAGS': ldflags,
                'PROTOBUF_LDFLAGS_EXTRA': ldflags
            }
            if self.platform == 'linux':
                return create_docker_jobspec(
                    self.name,
                    'tools/dockerfile/grpc_artifact_protoc',
                    'tools/run_tests/artifacts/build_artifact_protoc.sh',
                    environ=environ)
            else:
                environ[
                    'CXXFLAGS'] += ' -std=c++11 -stdlib=libc++ %s' % _MACOS_COMPAT_FLAG
                return create_jobspec(
                    self.name,
                    ['tools/run_tests/artifacts/build_artifact_protoc.sh'],
                    environ=environ,
                    timeout_seconds=60 * 60,
                    use_workspace=True)
        else:
            generator = 'Visual Studio 14 2015 Win64' if self.arch == 'x64' else 'Visual Studio 14 2015'
            return create_jobspec(
                self.name,
                ['tools\\run_tests\\artifacts\\build_artifact_protoc.bat'],
                environ={'generator': generator},
                use_workspace=True)

    def __str__(self):
        return self.name


def targets():
    """Gets list of supported targets"""
    return ([
        Cls(platform, arch)
        for Cls in (CSharpExtArtifact, ProtocArtifact)
        for platform in ('linux', 'macos', 'windows') for arch in ('x86', 'x64')
    ] + [
        CSharpExtArtifact('linux', 'android', arch_abi='arm64-v8a'),
        CSharpExtArtifact('linux', 'android', arch_abi='armeabi-v7a'),
        CSharpExtArtifact('linux', 'android', arch_abi='x86'),
        CSharpExtArtifact('macos', 'ios'),
        PythonArtifact('linux', 'x86', 'cp27-cp27m'),
        PythonArtifact('linux', 'x86', 'cp27-cp27mu'),
        PythonArtifact('linux', 'x86', 'cp34-cp34m'),
        PythonArtifact('linux', 'x86', 'cp35-cp35m'),
        PythonArtifact('linux', 'x86', 'cp36-cp36m'),
        PythonArtifact('linux', 'x86', 'cp37-cp37m'),
        PythonArtifact('linux_extra', 'armv7', '2.7'),
        PythonArtifact('linux_extra', 'armv7', '3.4'),
        PythonArtifact('linux_extra', 'armv7', '3.5'),
        PythonArtifact('linux_extra', 'armv7', '3.6'),
        PythonArtifact('linux_extra', 'armv6', '2.7'),
        PythonArtifact('linux_extra', 'armv6', '3.4'),
        PythonArtifact('linux_extra', 'armv6', '3.5'),
        PythonArtifact('linux_extra', 'armv6', '3.6'),
        PythonArtifact('linux', 'x64', 'cp27-cp27m'),
        PythonArtifact('linux', 'x64', 'cp27-cp27mu'),
        PythonArtifact('linux', 'x64', 'cp34-cp34m'),
        PythonArtifact('linux', 'x64', 'cp35-cp35m'),
        PythonArtifact('linux', 'x64', 'cp36-cp36m'),
        PythonArtifact('linux', 'x64', 'cp37-cp37m'),
        PythonArtifact('macos', 'x64', 'python2.7'),
        PythonArtifact('macos', 'x64', 'python3.4'),
        PythonArtifact('macos', 'x64', 'python3.5'),
        PythonArtifact('macos', 'x64', 'python3.6'),
        PythonArtifact('macos', 'x64', 'python3.7'),
        PythonArtifact('windows', 'x86', 'Python27_32bits'),
        PythonArtifact('windows', 'x86', 'Python34_32bits'),
        PythonArtifact('windows', 'x86', 'Python35_32bits'),
        PythonArtifact('windows', 'x86', 'Python36_32bits'),
        PythonArtifact('windows', 'x86', 'Python37_32bits'),
        PythonArtifact('windows', 'x64', 'Python27'),
        PythonArtifact('windows', 'x64', 'Python34'),
        PythonArtifact('windows', 'x64', 'Python35'),
        PythonArtifact('windows', 'x64', 'Python36'),
        PythonArtifact('windows', 'x64', 'Python37'),
        RubyArtifact('linux', 'x64'),
        RubyArtifact('macos', 'x64'),
        PHPArtifact('linux', 'x64'),
        PHPArtifact('macos', 'x64')
    ])
