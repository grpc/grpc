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
"""Definition of targets run distribution package tests."""

import os.path
import sys

sys.path.insert(0, os.path.abspath('..'))
import python_utils.jobset as jobset


def create_docker_jobspec(name,
                          dockerfile_dir,
                          shell_command,
                          environ={},
                          flake_retries=0,
                          timeout_retries=0,
                          copy_rel_path=None,
                          timeout_seconds=30 * 60):
    """Creates jobspec for a task running under docker."""
    environ = environ.copy()
    environ['RUN_COMMAND'] = shell_command
    # the entire repo will be cloned if copy_rel_path is not set.
    if copy_rel_path:
        environ['RELATIVE_COPY_PATH'] = copy_rel_path

    docker_args = []
    for k, v in list(environ.items()):
        docker_args += ['-e', '%s=%s' % (k, v)]
    docker_env = {
        'DOCKERFILE_DIR': dockerfile_dir,
        'DOCKER_RUN_SCRIPT': 'tools/run_tests/dockerize/docker_run.sh'
    }
    jobspec = jobset.JobSpec(
        cmdline=['tools/run_tests/dockerize/build_and_run_docker.sh'] +
        docker_args,
        environ=docker_env,
        shortname='distribtest.%s' % (name),
        timeout_seconds=timeout_seconds,
        flake_retries=flake_retries,
        timeout_retries=timeout_retries)
    return jobspec


def create_jobspec(name,
                   cmdline,
                   environ=None,
                   shell=False,
                   flake_retries=0,
                   timeout_retries=0,
                   use_workspace=False,
                   timeout_seconds=10 * 60):
    """Creates jobspec."""
    environ = environ.copy()
    if use_workspace:
        environ['WORKSPACE_NAME'] = 'workspace_%s' % name
        cmdline = ['bash', 'tools/run_tests/artifacts/run_in_workspace.sh'
                  ] + cmdline
    jobspec = jobset.JobSpec(cmdline=cmdline,
                             environ=environ,
                             shortname='distribtest.%s' % (name),
                             timeout_seconds=timeout_seconds,
                             flake_retries=flake_retries,
                             timeout_retries=timeout_retries,
                             shell=shell)
    return jobspec


class CSharpDistribTest(object):
    """Tests C# NuGet package"""

    def __init__(self,
                 platform,
                 arch,
                 docker_suffix=None,
                 use_dotnet_cli=False):
        self.name = 'csharp_%s_%s' % (platform, arch)
        self.platform = platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.labels = ['distribtest', 'csharp', platform, arch]
        self.script_suffix = ''
        if docker_suffix:
            self.name += '_%s' % docker_suffix
            self.labels.append(docker_suffix)
        if use_dotnet_cli:
            self.name += '_dotnetcli'
            self.script_suffix = '_dotnetcli'
            self.labels.append('dotnetcli')
        else:
            self.labels.append('olddotnet')

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        if self.platform == 'linux':
            return create_docker_jobspec(
                self.name,
                'tools/dockerfile/distribtest/csharp_%s_%s' %
                (self.docker_suffix, self.arch),
                'test/distrib/csharp/run_distrib_test%s.sh' %
                self.script_suffix,
                copy_rel_path='test/distrib')
        elif self.platform == 'macos':
            return create_jobspec(self.name, [
                'test/distrib/csharp/run_distrib_test%s.sh' % self.script_suffix
            ],
                                  environ={'EXTERNAL_GIT_ROOT': '../../../..'},
                                  use_workspace=True)
        elif self.platform == 'windows':
            if self.arch == 'x64':
                # Use double leading / as the first occurrence gets removed by msys bash
                # when invoking the .bat file (side-effect of posix path conversion)
                environ = {
                    'MSBUILD_EXTRA_ARGS': '//p:Platform=x64',
                    'DISTRIBTEST_OUTPATH': 'DistribTest\\bin\\x64\\Debug'
                }
            else:
                environ = {'DISTRIBTEST_OUTPATH': 'DistribTest\\bin\\Debug'}
            return create_jobspec(self.name, [
                'test\\distrib\\csharp\\run_distrib_test%s.bat' %
                self.script_suffix
            ],
                                  environ=environ,
                                  use_workspace=True)
        else:
            raise Exception("Not supported yet.")

    def __str__(self):
        return self.name


class PythonDistribTest(object):
    """Tests Python package"""

    def __init__(self, platform, arch, docker_suffix, source=False):
        self.source = source
        if source:
            self.name = 'python_dev_%s_%s_%s' % (platform, arch, docker_suffix)
        else:
            self.name = 'python_%s_%s_%s' % (platform, arch, docker_suffix)
        self.platform = platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.labels = ['distribtest', 'python', platform, arch, docker_suffix]

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        if not self.platform == 'linux':
            raise Exception("Not supported yet.")

        if self.source:
            return create_docker_jobspec(
                self.name,
                'tools/dockerfile/distribtest/python_dev_%s_%s' %
                (self.docker_suffix, self.arch),
                'test/distrib/python/run_source_distrib_test.sh',
                copy_rel_path='test/distrib')
        else:
            return create_docker_jobspec(
                self.name,
                'tools/dockerfile/distribtest/python_%s_%s' %
                (self.docker_suffix, self.arch),
                'test/distrib/python/run_binary_distrib_test.sh',
                copy_rel_path='test/distrib')

    def __str__(self):
        return self.name


class RubyDistribTest(object):
    """Tests Ruby package"""

    def __init__(self,
                 platform,
                 arch,
                 docker_suffix,
                 ruby_version=None,
                 source=False):
        self.package_type = 'binary'
        if source:
            self.package_type = 'source'
        self.name = 'ruby_%s_%s_%s_version_%s_package_type_%s' % (
            platform, arch, docker_suffix, ruby_version or
            'unspecified', self.package_type)
        self.platform = platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.ruby_version = ruby_version
        self.labels = ['distribtest', 'ruby', platform, arch, docker_suffix]

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        arch_to_gem_arch = {
            'x64': 'x86_64',
            'x86': 'x86',
        }
        if not self.platform == 'linux':
            raise Exception("Not supported yet.")

        dockerfile_name = 'tools/dockerfile/distribtest/ruby_%s_%s' % (
            self.docker_suffix, self.arch)
        if self.ruby_version is not None:
            dockerfile_name += '_%s' % self.ruby_version
        return create_docker_jobspec(
            self.name,
            dockerfile_name,
            'test/distrib/ruby/run_distrib_test.sh %s %s %s' %
            (arch_to_gem_arch[self.arch], self.platform, self.package_type),
            copy_rel_path='test/distrib')

    def __str__(self):
        return self.name


class PHP7DistribTest(object):
    """Tests PHP7 package"""

    def __init__(self, platform, arch, docker_suffix=None):
        self.name = 'php7_%s_%s_%s' % (platform, arch, docker_suffix)
        self.platform = platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.labels = ['distribtest', 'php', 'php7', platform, arch]
        if docker_suffix:
            self.labels.append(docker_suffix)

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        if self.platform == 'linux':
            return create_docker_jobspec(
                self.name,
                'tools/dockerfile/distribtest/php7_%s_%s' %
                (self.docker_suffix, self.arch),
                'test/distrib/php/run_distrib_test.sh',
                copy_rel_path='test/distrib')
        elif self.platform == 'macos':
            return create_jobspec(
                self.name, ['test/distrib/php/run_distrib_test_macos.sh'],
                environ={'EXTERNAL_GIT_ROOT': '../../../..'},
                timeout_seconds=15 * 60,
                use_workspace=True)
        else:
            raise Exception("Not supported yet.")

    def __str__(self):
        return self.name


class CppDistribTest(object):
    """Tests Cpp make install by building examples."""

    def __init__(self, platform, arch, docker_suffix=None, testcase=None):
        if platform == 'linux':
            self.name = 'cpp_%s_%s_%s_%s' % (platform, arch, docker_suffix,
                                             testcase)
        else:
            self.name = 'cpp_%s_%s_%s' % (platform, arch, testcase)
        self.platform = platform
        self.arch = arch
        self.docker_suffix = docker_suffix
        self.testcase = testcase
        self.labels = [
            'distribtest',
            'cpp',
            platform,
            arch,
            testcase,
        ]
        if docker_suffix:
            self.labels.append(docker_suffix)

    def pre_build_jobspecs(self):
        return []

    def build_jobspec(self):
        if self.platform == 'linux':
            return create_docker_jobspec(
                self.name,
                'tools/dockerfile/distribtest/cpp_%s_%s' %
                (self.docker_suffix, self.arch),
                'test/distrib/cpp/run_distrib_test_%s.sh' % self.testcase,
                timeout_seconds=45 * 60)
        elif self.platform == 'windows':
            return create_jobspec(
                self.name,
                ['test\\distrib\\cpp\\run_distrib_test_%s.bat' % self.testcase],
                environ={},
                timeout_seconds=30 * 60,
                use_workspace=True)
        else:
            raise Exception("Not supported yet.")

    def __str__(self):
        return self.name


def targets():
    """Gets list of supported targets"""
    return [
        # C++
        CppDistribTest('linux', 'x64', 'jessie', 'cmake_as_submodule'),
        CppDistribTest('linux', 'x64', 'stretch', 'cmake'),
        CppDistribTest('linux', 'x64', 'stretch', 'cmake_as_externalproject'),
        CppDistribTest('linux', 'x64', 'stretch', 'cmake_fetchcontent'),
        CppDistribTest('linux', 'x64', 'stretch', 'cmake_module_install'),
        CppDistribTest('linux', 'x64', 'stretch',
                       'cmake_module_install_pkgconfig'),
        CppDistribTest('linux', 'x64', 'stretch', 'cmake_pkgconfig'),
        CppDistribTest('linux', 'x64', 'stretch_aarch64_cross',
                       'cmake_aarch64_cross'),
        CppDistribTest('windows', 'x86', testcase='cmake'),
        CppDistribTest('windows', 'x86', testcase='cmake_as_externalproject'),
        # C#
        CSharpDistribTest('linux', 'x64', 'jessie'),
        CSharpDistribTest('linux', 'x64', 'stretch'),
        CSharpDistribTest('linux', 'x64', 'stretch', use_dotnet_cli=True),
        CSharpDistribTest('linux', 'x64', 'centos7'),
        CSharpDistribTest('linux', 'x64', 'ubuntu1604'),
        CSharpDistribTest('linux', 'x64', 'ubuntu1604', use_dotnet_cli=True),
        CSharpDistribTest('linux', 'x64', 'alpine', use_dotnet_cli=True),
        CSharpDistribTest('linux', 'x64', 'dotnet31', use_dotnet_cli=True),
        CSharpDistribTest('linux', 'x64', 'dotnet5', use_dotnet_cli=True),
        CSharpDistribTest('macos', 'x64'),
        CSharpDistribTest('windows', 'x86'),
        CSharpDistribTest('windows', 'x64'),
        # Python
        PythonDistribTest('linux', 'x64', 'buster'),
        PythonDistribTest('linux', 'x86', 'buster'),
        PythonDistribTest('linux', 'x64', 'centos7'),
        PythonDistribTest('linux', 'x64', 'fedora34'),
        PythonDistribTest('linux', 'x64', 'opensuse'),
        PythonDistribTest('linux', 'x64', 'arch'),
        PythonDistribTest('linux', 'x64', 'ubuntu1804'),
        PythonDistribTest('linux', 'aarch64', 'python38_buster'),
        PythonDistribTest('linux', 'x64', 'alpine3.7', source=True),
        PythonDistribTest('linux', 'x64', 'buster', source=True),
        PythonDistribTest('linux', 'x86', 'buster', source=True),
        PythonDistribTest('linux', 'x64', 'centos7', source=True),
        PythonDistribTest('linux', 'x64', 'fedora34', source=True),
        PythonDistribTest('linux', 'x64', 'arch', source=True),
        PythonDistribTest('linux', 'x64', 'ubuntu1804', source=True),
        # Ruby
        RubyDistribTest('linux', 'x64', 'stretch', ruby_version='ruby_2_5'),
        RubyDistribTest('linux', 'x64', 'stretch', ruby_version='ruby_2_6'),
        RubyDistribTest('linux', 'x64', 'stretch', ruby_version='ruby_2_7'),
        # TODO(apolcyn): add a ruby 3.0 test once protobuf adds support
        RubyDistribTest('linux',
                        'x64',
                        'stretch',
                        ruby_version='ruby_2_5',
                        source=True),
        RubyDistribTest('linux', 'x64', 'centos7'),
        RubyDistribTest('linux', 'x64', 'ubuntu1604'),
        RubyDistribTest('linux', 'x64', 'ubuntu1804'),
        # PHP7
        PHP7DistribTest('linux', 'x64', 'stretch'),
        PHP7DistribTest('macos', 'x64'),
    ]
