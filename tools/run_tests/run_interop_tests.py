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
"""Run interop (cross-language) tests in parallel."""

from __future__ import print_function

import argparse
import atexit
import itertools
import json
import multiprocessing
import os
import re
import subprocess
import sys
import tempfile
import time
import traceback
import uuid

import six

import python_utils.dockerjob as dockerjob
import python_utils.jobset as jobset
import python_utils.report_utils as report_utils

# It's ok to not import because this is only necessary to upload results to BQ.
try:
    from python_utils.upload_test_results import upload_interop_results_to_bq
except ImportError as e:
    print(e)

# Docker doesn't clean up after itself, so we do it on exit.
atexit.register(lambda: subprocess.call(['stty', 'echo']))

ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

_DEFAULT_SERVER_PORT = 8080

_SKIP_CLIENT_COMPRESSION = [
    'client_compressed_unary', 'client_compressed_streaming'
]

_SKIP_SERVER_COMPRESSION = [
    'server_compressed_unary', 'server_compressed_streaming'
]

_SKIP_COMPRESSION = _SKIP_CLIENT_COMPRESSION + _SKIP_SERVER_COMPRESSION

_SKIP_ADVANCED = [
    'status_code_and_message', 'custom_metadata', 'unimplemented_method',
    'unimplemented_service'
]

_SKIP_SPECIAL_STATUS_MESSAGE = ['special_status_message']

_ORCA_TEST_CASES = ['orca_per_rpc', 'orca_oob']

_GOOGLE_DEFAULT_CREDS_TEST_CASE = 'google_default_credentials'

_SKIP_GOOGLE_DEFAULT_CREDS = [
    _GOOGLE_DEFAULT_CREDS_TEST_CASE,
]

_COMPUTE_ENGINE_CHANNEL_CREDS_TEST_CASE = 'compute_engine_channel_credentials'

_SKIP_COMPUTE_ENGINE_CHANNEL_CREDS = [
    _COMPUTE_ENGINE_CHANNEL_CREDS_TEST_CASE,
]

_TEST_TIMEOUT = 3 * 60

# disable this test on core-based languages,
# see https://github.com/grpc/grpc/issues/9779
_SKIP_DATA_FRAME_PADDING = ['data_frame_padding']

# report suffix "sponge_log.xml" is important for reports to get picked up by internal CI
_DOCKER_BUILD_XML_REPORT = 'interop_docker_build/sponge_log.xml'
_TESTS_XML_REPORT = 'interop_test/sponge_log.xml'


class CXXLanguage:

    def __init__(self):
        self.client_cwd = None
        self.server_cwd = None
        self.http2_cwd = None
        self.safename = 'cxx'

    def client_cmd(self, args):
        return ['cmake/build/interop_client'] + args

    def client_cmd_http2interop(self, args):
        return ['cmake/build/http2_client'] + args

    def cloud_to_prod_env(self):
        return {}

    def server_cmd(self, args):
        return ['cmake/build/interop_server'] + args

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _SKIP_DATA_FRAME_PADDING + \
               _SKIP_SPECIAL_STATUS_MESSAGE + \
               _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS

    def unimplemented_test_cases_server(self):
        return []

    def __str__(self):
        return 'c++'


class AspNetCoreLanguage:

    def __init__(self):
        self.client_cwd = '../grpc-dotnet/output/InteropTestsClient'
        self.server_cwd = '../grpc-dotnet/output/InteropTestsWebsite'
        self.safename = str(self)

    def cloud_to_prod_env(self):
        return {}

    def client_cmd(self, args):
        return ['dotnet', 'exec', 'InteropTestsClient.dll'] + args

    def server_cmd(self, args):
        return ['dotnet', 'exec', 'InteropTestsWebsite.dll'] + args

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _SKIP_GOOGLE_DEFAULT_CREDS + \
               _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS + \
               _ORCA_TEST_CASES

    def unimplemented_test_cases_server(self):
        return _ORCA_TEST_CASES

    def __str__(self):
        return 'aspnetcore'


class DartLanguage:

    def __init__(self):
        self.client_cwd = '../grpc-dart/interop'
        self.server_cwd = '../grpc-dart/interop'
        self.http2_cwd = '../grpc-dart/interop'
        self.safename = str(self)

    def client_cmd(self, args):
        return ['dart', 'bin/client.dart'] + args

    def cloud_to_prod_env(self):
        return {}

    def server_cmd(self, args):
        return ['dart', 'bin/server.dart'] + args

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _SKIP_COMPRESSION + \
               _SKIP_SPECIAL_STATUS_MESSAGE + \
               _SKIP_GOOGLE_DEFAULT_CREDS + \
               _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS + \
               _ORCA_TEST_CASES

    def unimplemented_test_cases_server(self):
        return _SKIP_COMPRESSION + _SKIP_SPECIAL_STATUS_MESSAGE + _ORCA_TEST_CASES

    def __str__(self):
        return 'dart'


class JavaLanguage:

    def __init__(self):
        self.client_cwd = '../grpc-java'
        self.server_cwd = '../grpc-java'
        self.http2_cwd = '../grpc-java'
        self.safename = str(self)

    def client_cmd(self, args):
        return ['./run-test-client.sh'] + args

    def client_cmd_http2interop(self, args):
        return [
            './interop-testing/build/install/grpc-interop-testing/bin/http2-client'
        ] + args

    def cloud_to_prod_env(self):
        return {}

    def server_cmd(self, args):
        return ['./run-test-server.sh'] + args

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return []

    def unimplemented_test_cases_server(self):
        # Does not support CompressedRequest feature.
        # Only supports CompressedResponse feature for unary.
        return _SKIP_CLIENT_COMPRESSION + ['server_compressed_streaming']

    def __str__(self):
        return 'java'


class JavaOkHttpClient:

    def __init__(self):
        self.client_cwd = '../grpc-java'
        self.safename = 'java'

    def client_cmd(self, args):
        return ['./run-test-client.sh', '--use_okhttp=true'] + args

    def cloud_to_prod_env(self):
        return {}

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _SKIP_DATA_FRAME_PADDING

    def __str__(self):
        return 'javaokhttp'


class GoLanguage:

    def __init__(self):
        # TODO: this relies on running inside docker
        self.client_cwd = '/go/src/google.golang.org/grpc/interop/client'
        self.server_cwd = '/go/src/google.golang.org/grpc/interop/server'
        self.http2_cwd = '/go/src/google.golang.org/grpc/interop/http2'
        self.safename = str(self)

    def client_cmd(self, args):
        return ['go', 'run', 'client.go'] + args

    def client_cmd_http2interop(self, args):
        return ['go', 'run', 'negative_http2_client.go'] + args

    def cloud_to_prod_env(self):
        return {}

    def server_cmd(self, args):
        return ['go', 'run', 'server.go'] + args

    def global_env(self):
        return {'GO111MODULE': 'on'}

    def unimplemented_test_cases(self):
        return _SKIP_COMPRESSION

    def unimplemented_test_cases_server(self):
        return _SKIP_COMPRESSION

    def __str__(self):
        return 'go'


class Http2Server:
    """Represents the HTTP/2 Interop Test server

  This pretends to be a language in order to be built and run, but really it
  isn't.
  """

    def __init__(self):
        self.server_cwd = None
        self.safename = str(self)

    def server_cmd(self, args):
        return ['python test/http2_test/http2_test_server.py']

    def cloud_to_prod_env(self):
        return {}

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _TEST_CASES + \
            _SKIP_DATA_FRAME_PADDING + \
            _SKIP_SPECIAL_STATUS_MESSAGE + \
            _SKIP_GOOGLE_DEFAULT_CREDS + \
            _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS

    def unimplemented_test_cases_server(self):
        return _TEST_CASES

    def __str__(self):
        return 'http2'


class Http2Client:
    """Represents the HTTP/2 Interop Test

  This pretends to be a language in order to be built and run, but really it
  isn't.
  """

    def __init__(self):
        self.client_cwd = None
        self.safename = str(self)

    def client_cmd(self, args):
        return ['tools/http2_interop/http2_interop.test', '-test.v'] + args

    def cloud_to_prod_env(self):
        return {}

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _TEST_CASES + \
            _SKIP_SPECIAL_STATUS_MESSAGE + \
            _SKIP_GOOGLE_DEFAULT_CREDS + \
            _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS

    def unimplemented_test_cases_server(self):
        return _TEST_CASES

    def __str__(self):
        return 'http2'


class NodeLanguage:

    def __init__(self):
        self.client_cwd = '../../../../home/appuser/grpc-node'
        self.server_cwd = '../../../../home/appuser/grpc-node'
        self.safename = str(self)

    def client_cmd(self, args):
        return [
            'packages/grpc-native-core/deps/grpc/tools/run_tests/interop/with_nvm.sh',
            'node', '--require', './test/fixtures/native_native',
            'test/interop/interop_client.js'
        ] + args

    def cloud_to_prod_env(self):
        return {}

    def server_cmd(self, args):
        return [
            'packages/grpc-native-core/deps/grpc/tools/run_tests/interop/with_nvm.sh',
            'node', '--require', './test/fixtures/native_native',
            'test/interop/interop_server.js'
        ] + args

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _SKIP_COMPRESSION + \
               _SKIP_DATA_FRAME_PADDING + \
               _SKIP_GOOGLE_DEFAULT_CREDS + \
               _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS + \
               _ORCA_TEST_CASES

    def unimplemented_test_cases_server(self):
        return _SKIP_COMPRESSION + \
               _ORCA_TEST_CASES

    def __str__(self):
        return 'node'


class NodePureJSLanguage:

    def __init__(self):
        self.client_cwd = '../../../../home/appuser/grpc-node'
        self.server_cwd = '../../../../home/appuser/grpc-node'
        self.safename = str(self)

    def client_cmd(self, args):
        return [
            'packages/grpc-native-core/deps/grpc/tools/run_tests/interop/with_nvm.sh',
            'node', '--require', './test/fixtures/js_js',
            'test/interop/interop_client.js'
        ] + args

    def cloud_to_prod_env(self):
        return {}

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _SKIP_COMPRESSION + \
               _SKIP_DATA_FRAME_PADDING + \
               _SKIP_GOOGLE_DEFAULT_CREDS + \
               _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS + \
               _ORCA_TEST_CASES

    def unimplemented_test_cases_server(self):
        return _ORCA_TEST_CASES

    def __str__(self):
        return 'nodepurejs'


class PHP7Language:

    def __init__(self):
        self.client_cwd = None
        self.server_cwd = None
        self.safename = str(self)

    def client_cmd(self, args):
        return ['src/php/bin/interop_client.sh'] + args

    def cloud_to_prod_env(self):
        return {}

    def server_cmd(self, args):
        return ['src/php/bin/interop_server.sh'] + args

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _SKIP_SERVER_COMPRESSION + \
               _SKIP_DATA_FRAME_PADDING + \
               _SKIP_GOOGLE_DEFAULT_CREDS + \
               _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS + \
               _ORCA_TEST_CASES

    def unimplemented_test_cases_server(self):
        return _SKIP_COMPRESSION + \
               _ORCA_TEST_CASES

    def __str__(self):
        return 'php7'


class ObjcLanguage:

    def __init__(self):
        self.client_cwd = 'src/objective-c/tests'
        self.safename = str(self)

    def client_cmd(self, args):
        # from args, extract the server port and craft xcodebuild command out of it
        for arg in args:
            port = re.search('--server_port=(\d+)', arg)
            if port:
                portnum = port.group(1)
                cmdline = 'pod install && xcodebuild -workspace Tests.xcworkspace -scheme InteropTestsLocalSSL -destination name="iPhone 6" HOST_PORT_LOCALSSL=localhost:%s test' % portnum
                return [cmdline]

    def cloud_to_prod_env(self):
        return {}

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        # ObjC test runs all cases with the same command. It ignores the testcase
        # cmdline argument. Here we return all but one test cases as unimplemented,
        # and depend upon ObjC test's behavior that it runs all cases even when
        # we tell it to run just one.
        return _TEST_CASES[1:] + \
               _SKIP_COMPRESSION + \
               _SKIP_DATA_FRAME_PADDING + \
               _SKIP_SPECIAL_STATUS_MESSAGE + \
               _SKIP_GOOGLE_DEFAULT_CREDS + \
               _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS + \
               _ORCA_TEST_CASES

    def unimplemented_test_cases_server(self):
        return _SKIP_COMPRESSION + \
               _ORCA_TEST_CASES

    def __str__(self):
        return 'objc'


class RubyLanguage:

    def __init__(self):
        self.client_cwd = None
        self.server_cwd = None
        self.safename = str(self)

    def client_cmd(self, args):
        return [
            'tools/run_tests/interop/with_rvm.sh', 'ruby',
            'src/ruby/pb/test/client.rb'
        ] + args

    def cloud_to_prod_env(self):
        return {}

    def server_cmd(self, args):
        return [
            'tools/run_tests/interop/with_rvm.sh', 'ruby',
            'src/ruby/pb/test/server.rb'
        ] + args

    def global_env(self):
        return {}

    def unimplemented_test_cases(self):
        return _SKIP_SERVER_COMPRESSION + \
               _SKIP_DATA_FRAME_PADDING + \
               _SKIP_SPECIAL_STATUS_MESSAGE + \
               _SKIP_GOOGLE_DEFAULT_CREDS + \
               _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS + \
               _ORCA_TEST_CASES

    def unimplemented_test_cases_server(self):
        return _SKIP_COMPRESSION + \
               _ORCA_TEST_CASES

    def __str__(self):
        return 'ruby'


_PYTHON_BINARY = 'py39/bin/python'


class PythonLanguage:

    def __init__(self):
        self.client_cwd = None
        self.server_cwd = None
        self.http2_cwd = None
        self.safename = str(self)

    def client_cmd(self, args):
        return [
            _PYTHON_BINARY, 'src/python/grpcio_tests/setup.py', 'run_interop',
            '--client', '--args="{}"'.format(' '.join(args))
        ]

    def client_cmd_http2interop(self, args):
        return [
            _PYTHON_BINARY,
            'src/python/grpcio_tests/tests/http2/negative_http2_client.py',
        ] + args

    def cloud_to_prod_env(self):
        return {}

    def server_cmd(self, args):
        return [
            _PYTHON_BINARY, 'src/python/grpcio_tests/setup.py', 'run_interop',
            '--server', '--args="{}"'.format(' '.join(args))
        ]

    def global_env(self):
        return {
            'LD_LIBRARY_PATH': '{}/libs/opt'.format(DOCKER_WORKDIR_ROOT),
            'PYTHONPATH': '{}/src/python/gens'.format(DOCKER_WORKDIR_ROOT)
        }

    def unimplemented_test_cases(self):
        return _SKIP_COMPRESSION + \
               _SKIP_DATA_FRAME_PADDING + \
               _SKIP_GOOGLE_DEFAULT_CREDS + \
               _SKIP_COMPUTE_ENGINE_CHANNEL_CREDS + \
               _ORCA_TEST_CASES

    def unimplemented_test_cases_server(self):
        return _SKIP_COMPRESSION + \
               _ORCA_TEST_CASES

    def __str__(self):
        return 'python'


class PythonAsyncIOLanguage:

    def __init__(self):
        self.client_cwd = None
        self.server_cwd = None
        self.http2_cwd = None
        self.safename = str(self)

    def client_cmd(self, args):
        return [
            _PYTHON_BINARY, 'src/python/grpcio_tests/setup.py', 'run_interop',
            '--use-asyncio', '--client', '--args="{}"'.format(' '.join(args))
        ]

    def client_cmd_http2interop(self, args):
        return [
            _PYTHON_BINARY,
            'src/python/grpcio_tests/tests/http2/negative_http2_client.py',
        ] + args

    def cloud_to_prod_env(self):
        return {}

    def server_cmd(self, args):
        return [
            _PYTHON_BINARY, 'src/python/grpcio_tests/setup.py', 'run_interop',
            '--use-asyncio', '--server', '--args="{}"'.format(' '.join(args))
        ]

    def global_env(self):
        return {
            'LD_LIBRARY_PATH': '{}/libs/opt'.format(DOCKER_WORKDIR_ROOT),
            'PYTHONPATH': '{}/src/python/gens'.format(DOCKER_WORKDIR_ROOT)
        }

    def unimplemented_test_cases(self):
        # TODO(https://github.com/grpc/grpc/issues/21707)
        return _SKIP_COMPRESSION + \
               _SKIP_DATA_FRAME_PADDING + \
               _AUTH_TEST_CASES + \
               ['timeout_on_sleeping_server'] + \
               _ORCA_TEST_CASES

    def unimplemented_test_cases_server(self):
        # TODO(https://github.com/grpc/grpc/issues/21749)
        return _TEST_CASES + \
            _AUTH_TEST_CASES + \
            _HTTP2_TEST_CASES + \
            _HTTP2_SERVER_TEST_CASES

    def __str__(self):
        return 'pythonasyncio'


_LANGUAGES = {
    'c++': CXXLanguage(),
    'aspnetcore': AspNetCoreLanguage(),
    'dart': DartLanguage(),
    'go': GoLanguage(),
    'java': JavaLanguage(),
    'javaokhttp': JavaOkHttpClient(),
    'node': NodeLanguage(),
    'nodepurejs': NodePureJSLanguage(),
    'php7': PHP7Language(),
    'objc': ObjcLanguage(),
    'ruby': RubyLanguage(),
    'python': PythonLanguage(),
    'pythonasyncio': PythonAsyncIOLanguage(),
}

# languages supported as cloud_to_cloud servers
_SERVERS = [
    'c++', 'node', 'aspnetcore', 'java', 'go', 'ruby', 'python', 'dart',
    'pythonasyncio', 'php7'
]

_TEST_CASES = [
    'large_unary', 'empty_unary', 'ping_pong', 'empty_stream',
    'client_streaming', 'server_streaming', 'cancel_after_begin',
    'cancel_after_first_response', 'timeout_on_sleeping_server',
    'custom_metadata', 'status_code_and_message', 'unimplemented_method',
    'client_compressed_unary', 'server_compressed_unary',
    'client_compressed_streaming', 'server_compressed_streaming',
    'unimplemented_service', 'special_status_message', 'orca_per_rpc',
    'orca_oob'
]

_AUTH_TEST_CASES = [
    'compute_engine_creds',
    'jwt_token_creds',
    'oauth2_auth_token',
    'per_rpc_creds',
    _GOOGLE_DEFAULT_CREDS_TEST_CASE,
    _COMPUTE_ENGINE_CHANNEL_CREDS_TEST_CASE,
]

_HTTP2_TEST_CASES = ['tls', 'framing']

_HTTP2_SERVER_TEST_CASES = [
    'rst_after_header', 'rst_after_data', 'rst_during_data', 'goaway', 'ping',
    'max_streams', 'data_frame_padding', 'no_df_padding_sanity_test'
]

_GRPC_CLIENT_TEST_CASES_FOR_HTTP2_SERVER_TEST_CASES = {
    'data_frame_padding': 'large_unary',
    'no_df_padding_sanity_test': 'large_unary'
}

_HTTP2_SERVER_TEST_CASES_THAT_USE_GRPC_CLIENTS = list(
    _GRPC_CLIENT_TEST_CASES_FOR_HTTP2_SERVER_TEST_CASES.keys())

_LANGUAGES_WITH_HTTP2_CLIENTS_FOR_HTTP2_SERVER_TEST_CASES = [
    'java', 'go', 'python', 'c++'
]

_LANGUAGES_FOR_ALTS_TEST_CASES = ['java', 'go', 'c++', 'python']

_SERVERS_FOR_ALTS_TEST_CASES = ['java', 'go', 'c++', 'python']

_TRANSPORT_SECURITY_OPTIONS = ['tls', 'alts', 'insecure']

_CUSTOM_CREDENTIALS_TYPE_OPTIONS = [
    'tls', 'google_default_credentials', 'compute_engine_channel_creds'
]

DOCKER_WORKDIR_ROOT = '/var/local/git/grpc'


def docker_run_cmdline(cmdline, image, docker_args=[], cwd=None, environ=None):
    """Wraps given cmdline array to create 'docker run' cmdline from it."""

    # don't use '-t' even when TTY is available, since that would break
    # the testcases generated by tools/interop_matrix/create_testcases.sh
    docker_cmdline = ['docker', 'run', '-i', '--rm=true']

    # turn environ into -e docker args
    if environ:
        for k, v in list(environ.items()):
            docker_cmdline += ['-e', '%s=%s' % (k, v)]

    # set working directory
    workdir = DOCKER_WORKDIR_ROOT
    if cwd:
        workdir = os.path.join(workdir, cwd)
    docker_cmdline += ['-w', workdir]

    docker_cmdline += docker_args + [image] + cmdline
    return docker_cmdline


def manual_cmdline(docker_cmdline, docker_image):
    """Returns docker cmdline adjusted for manual invocation."""
    print_cmdline = []
    for item in docker_cmdline:
        if item.startswith('--name='):
            continue
        if item == docker_image:
            item = "$docker_image"
        item = item.replace('"', '\\"')
        # add quotes when necessary
        if any(character.isspace() for character in item):
            item = "\"%s\"" % item
        print_cmdline.append(item)
    return ' '.join(print_cmdline)


def write_cmdlog_maybe(cmdlog, filename):
    """Returns docker cmdline adjusted for manual invocation."""
    if cmdlog:
        with open(filename, 'w') as logfile:
            logfile.write('#!/bin/bash\n')
            logfile.write('# DO NOT MODIFY\n')
            logfile.write(
                '# This file is generated by run_interop_tests.py/create_testcases.sh\n'
            )
            logfile.writelines("%s\n" % line for line in cmdlog)
        print('Command log written to file %s' % filename)


def bash_cmdline(cmdline):
    """Creates bash -c cmdline from args list."""
    # Use login shell:
    # * makes error messages clearer if executables are missing
    return ['bash', '-c', ' '.join(cmdline)]


def compute_engine_creds_required(language, test_case):
    """Returns True if given test requires access to compute engine creds."""
    language = str(language)
    if test_case == 'compute_engine_creds':
        return True
    if test_case == 'oauth2_auth_token' and language == 'c++':
        # C++ oauth2 test uses GCE creds because C++ only supports JWT
        return True
    return False


def auth_options(language, test_case, google_default_creds_use_key_file,
                 service_account_key_file, default_service_account):
    """Returns (cmdline, env) tuple with cloud_to_prod_auth test options."""

    language = str(language)
    cmdargs = []
    env = {}

    oauth_scope_arg = '--oauth_scope=https://www.googleapis.com/auth/xapi.zoo'
    key_file_arg = '--service_account_key_file=%s' % service_account_key_file
    default_account_arg = '--default_service_account=%s' % default_service_account

    if test_case in ['jwt_token_creds', 'per_rpc_creds', 'oauth2_auth_token']:
        if language in [
                'aspnetcore', 'node', 'php7', 'python', 'ruby', 'nodepurejs'
        ]:
            env['GOOGLE_APPLICATION_CREDENTIALS'] = service_account_key_file
        else:
            cmdargs += [key_file_arg]

    if test_case in ['per_rpc_creds', 'oauth2_auth_token']:
        cmdargs += [oauth_scope_arg]

    if test_case == 'oauth2_auth_token' and language == 'c++':
        # C++ oauth2 test uses GCE creds and thus needs to know the default account
        cmdargs += [default_account_arg]

    if test_case == 'compute_engine_creds':
        cmdargs += [oauth_scope_arg, default_account_arg]

    if test_case == _GOOGLE_DEFAULT_CREDS_TEST_CASE:
        if google_default_creds_use_key_file:
            env['GOOGLE_APPLICATION_CREDENTIALS'] = service_account_key_file
        cmdargs += [default_account_arg]

    if test_case == _COMPUTE_ENGINE_CHANNEL_CREDS_TEST_CASE:
        cmdargs += [default_account_arg]

    return (cmdargs, env)


def _job_kill_handler(job):
    if job._spec.container_name:
        dockerjob.docker_kill(job._spec.container_name)
        # When the job times out and we decide to kill it,
        # we need to wait a before restarting the job
        # to prevent "container name already in use" error.
        # TODO(jtattermusch): figure out a cleaner way to this.
        time.sleep(2)


def cloud_to_prod_jobspec(language,
                          test_case,
                          server_host_nickname,
                          server_host,
                          google_default_creds_use_key_file,
                          docker_image=None,
                          auth=False,
                          manual_cmd_log=None,
                          service_account_key_file=None,
                          default_service_account=None,
                          transport_security='tls'):
    """Creates jobspec for cloud-to-prod interop test"""
    container_name = None
    cmdargs = [
        '--server_host=%s' % server_host, '--server_port=443',
        '--test_case=%s' % test_case
    ]
    if transport_security == 'tls':
        transport_security_options = ['--use_tls=true']
    elif transport_security == 'google_default_credentials' and str(
            language) in ['c++', 'go', 'java', 'javaokhttp']:
        transport_security_options = [
            '--custom_credentials_type=google_default_credentials'
        ]
    elif transport_security == 'compute_engine_channel_creds' and str(
            language) in ['go', 'java', 'javaokhttp']:
        transport_security_options = [
            '--custom_credentials_type=compute_engine_channel_creds'
        ]
    else:
        print(
            'Invalid transport security option %s in cloud_to_prod_jobspec. Lang: %s'
            % (str(language), transport_security))
        sys.exit(1)
    cmdargs = cmdargs + transport_security_options
    environ = dict(language.cloud_to_prod_env(), **language.global_env())
    if auth:
        auth_cmdargs, auth_env = auth_options(
            language, test_case, google_default_creds_use_key_file,
            service_account_key_file, default_service_account)
        cmdargs += auth_cmdargs
        environ.update(auth_env)
    cmdline = bash_cmdline(language.client_cmd(cmdargs))
    cwd = language.client_cwd

    if docker_image:
        container_name = dockerjob.random_name('interop_client_%s' %
                                               language.safename)
        cmdline = docker_run_cmdline(
            cmdline,
            image=docker_image,
            cwd=cwd,
            environ=environ,
            docker_args=['--net=host',
                         '--name=%s' % container_name])
        if manual_cmd_log is not None:
            if manual_cmd_log == []:
                manual_cmd_log.append('echo "Testing ${docker_image:=%s}"' %
                                      docker_image)
            manual_cmd_log.append(manual_cmdline(cmdline, docker_image))
        cwd = None
        environ = None

    suite_name = 'cloud_to_prod_auth' if auth else 'cloud_to_prod'
    test_job = jobset.JobSpec(cmdline=cmdline,
                              cwd=cwd,
                              environ=environ,
                              shortname='%s:%s:%s:%s:%s' %
                              (suite_name, language, server_host_nickname,
                               test_case, transport_security),
                              timeout_seconds=_TEST_TIMEOUT,
                              flake_retries=4 if args.allow_flakes else 0,
                              timeout_retries=2 if args.allow_flakes else 0,
                              kill_handler=_job_kill_handler)
    if docker_image:
        test_job.container_name = container_name
    return test_job


def cloud_to_cloud_jobspec(language,
                           test_case,
                           server_name,
                           server_host,
                           server_port,
                           docker_image=None,
                           transport_security='tls',
                           manual_cmd_log=None):
    """Creates jobspec for cloud-to-cloud interop test"""
    interop_only_options = [
        '--server_host_override=foo.test.google.fr',
        '--use_test_ca=true',
    ]
    if transport_security == 'tls':
        interop_only_options += ['--use_tls=true']
    elif transport_security == 'alts':
        interop_only_options += ['--use_tls=false', '--use_alts=true']
    elif transport_security == 'insecure':
        interop_only_options += ['--use_tls=false']
    else:
        print(
            'Invalid transport security option %s in cloud_to_cloud_jobspec.' %
            transport_security)
        sys.exit(1)

    client_test_case = test_case
    if test_case in _HTTP2_SERVER_TEST_CASES_THAT_USE_GRPC_CLIENTS:
        client_test_case = _GRPC_CLIENT_TEST_CASES_FOR_HTTP2_SERVER_TEST_CASES[
            test_case]
    if client_test_case in language.unimplemented_test_cases():
        print('asking client %s to run unimplemented test case %s' %
              (repr(language), client_test_case))
        sys.exit(1)

    if test_case in _ORCA_TEST_CASES:
        interop_only_options += [
            '--service_config_json=\'{"loadBalancingConfig":[{"test_backend_metrics_load_balancer":{}}]}\''
        ]

    common_options = [
        '--test_case=%s' % client_test_case,
        '--server_host=%s' % server_host,
        '--server_port=%s' % server_port,
    ]

    if test_case in _HTTP2_SERVER_TEST_CASES:
        if test_case in _HTTP2_SERVER_TEST_CASES_THAT_USE_GRPC_CLIENTS:
            client_options = interop_only_options + common_options
            cmdline = bash_cmdline(language.client_cmd(client_options))
            cwd = language.client_cwd
        else:
            cmdline = bash_cmdline(
                language.client_cmd_http2interop(common_options))
            cwd = language.http2_cwd
    else:
        cmdline = bash_cmdline(
            language.client_cmd(common_options + interop_only_options))
        cwd = language.client_cwd

    environ = language.global_env()
    if docker_image and language.safename != 'objc':
        # we can't run client in docker for objc.
        container_name = dockerjob.random_name('interop_client_%s' %
                                               language.safename)
        cmdline = docker_run_cmdline(
            cmdline,
            image=docker_image,
            environ=environ,
            cwd=cwd,
            docker_args=['--net=host',
                         '--name=%s' % container_name])
        if manual_cmd_log is not None:
            if manual_cmd_log == []:
                manual_cmd_log.append('echo "Testing ${docker_image:=%s}"' %
                                      docker_image)
            manual_cmd_log.append(manual_cmdline(cmdline, docker_image))
        cwd = None

    test_job = jobset.JobSpec(
        cmdline=cmdline,
        cwd=cwd,
        environ=environ,
        shortname='cloud_to_cloud:%s:%s_server:%s:%s' %
        (language, server_name, test_case, transport_security),
        timeout_seconds=_TEST_TIMEOUT,
        flake_retries=4 if args.allow_flakes else 0,
        timeout_retries=2 if args.allow_flakes else 0,
        kill_handler=_job_kill_handler)
    if docker_image:
        test_job.container_name = container_name
    return test_job


def server_jobspec(language,
                   docker_image,
                   transport_security='tls',
                   manual_cmd_log=None):
    """Create jobspec for running a server"""
    container_name = dockerjob.random_name('interop_server_%s' %
                                           language.safename)
    server_cmd = ['--port=%s' % _DEFAULT_SERVER_PORT]
    if transport_security == 'tls':
        server_cmd += ['--use_tls=true']
    elif transport_security == 'alts':
        server_cmd += ['--use_tls=false', '--use_alts=true']
    elif transport_security == 'insecure':
        server_cmd += ['--use_tls=false']
    else:
        print('Invalid transport security option %s in server_jobspec.' %
              transport_security)
        sys.exit(1)
    cmdline = bash_cmdline(language.server_cmd(server_cmd))
    environ = language.global_env()
    docker_args = ['--name=%s' % container_name]
    if language.safename == 'http2':
        # we are running the http2 interop server. Open next N ports beginning
        # with the server port. These ports are used for http2 interop test
        # (one test case per port).
        docker_args += list(
            itertools.chain.from_iterable(
                ('-p', str(_DEFAULT_SERVER_PORT + i))
                for i in range(len(_HTTP2_SERVER_TEST_CASES))))
        # Enable docker's healthcheck mechanism.
        # This runs a Python script inside the container every second. The script
        # pings the http2 server to verify it is ready. The 'health-retries' flag
        # specifies the number of consecutive failures before docker will report
        # the container's status as 'unhealthy'. Prior to the first 'health_retries'
        # failures or the first success, the status will be 'starting'. 'docker ps'
        # or 'docker inspect' can be used to see the health of the container on the
        # command line.
        docker_args += [
            '--health-cmd=python test/http2_test/http2_server_health_check.py '
            '--server_host=%s --server_port=%d' %
            ('localhost', _DEFAULT_SERVER_PORT),
            '--health-interval=1s',
            '--health-retries=5',
            '--health-timeout=10s',
        ]

    else:
        docker_args += ['-p', str(_DEFAULT_SERVER_PORT)]

    docker_cmdline = docker_run_cmdline(cmdline,
                                        image=docker_image,
                                        cwd=language.server_cwd,
                                        environ=environ,
                                        docker_args=docker_args)
    if manual_cmd_log is not None:
        if manual_cmd_log == []:
            manual_cmd_log.append('echo "Testing ${docker_image:=%s}"' %
                                  docker_image)
        manual_cmd_log.append(manual_cmdline(docker_cmdline, docker_image))
    server_job = jobset.JobSpec(cmdline=docker_cmdline,
                                environ=environ,
                                shortname='interop_server_%s' % language,
                                timeout_seconds=30 * 60)
    server_job.container_name = container_name
    return server_job


def build_interop_image_jobspec(language, tag=None):
    """Creates jobspec for building interop docker image for a language"""
    if not tag:
        tag = 'grpc_interop_%s:%s' % (language.safename, uuid.uuid4())
    env = {
        'INTEROP_IMAGE': tag,
        'BASE_NAME': 'grpc_interop_%s' % language.safename
    }
    build_job = jobset.JobSpec(
        cmdline=['tools/run_tests/dockerize/build_interop_image.sh'],
        environ=env,
        shortname='build_docker_%s' % (language),
        timeout_seconds=30 * 60)
    build_job.tag = tag
    return build_job


def aggregate_http2_results(stdout):
    match = re.search(r'\{"cases[^\]]*\]\}', stdout)
    if not match:
        return None

    results = json.loads(match.group(0))
    skipped = 0
    passed = 0
    failed = 0
    failed_cases = []
    for case in results['cases']:
        if case.get('skipped', False):
            skipped += 1
        else:
            if case.get('passed', False):
                passed += 1
            else:
                failed += 1
                failed_cases.append(case.get('name', "NONAME"))
    return {
        'passed': passed,
        'failed': failed,
        'skipped': skipped,
        'failed_cases': ', '.join(failed_cases),
        'percent': 1.0 * passed / (passed + failed)
    }


# A dictionary of prod servers to test against.
# See go/grpc-interop-tests (internal-only) for details.
prod_servers = {
    'default': 'grpc-test.sandbox.googleapis.com',
    'gateway_v4': 'grpc-test4.sandbox.googleapis.com',
}

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-l',
                  '--language',
                  choices=['all'] + sorted(_LANGUAGES),
                  nargs='+',
                  default=['all'],
                  help='Clients to run. Objc client can be only run on OSX.')
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
argp.add_argument('--cloud_to_prod',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Run cloud_to_prod tests.')
argp.add_argument('--cloud_to_prod_auth',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Run cloud_to_prod_auth tests.')
argp.add_argument('--google_default_creds_use_key_file',
                  default=False,
                  action='store_const',
                  const=True,
                  help=('Whether or not we should use a key file for the '
                        'google_default_credentials test case, e.g. by '
                        'setting env var GOOGLE_APPLICATION_CREDENTIALS.'))
argp.add_argument('--prod_servers',
                  choices=list(prod_servers.keys()),
                  default=['default'],
                  nargs='+',
                  help=('The servers to run cloud_to_prod and '
                        'cloud_to_prod_auth tests against.'))
argp.add_argument('-s',
                  '--server',
                  choices=['all'] + sorted(_SERVERS),
                  nargs='+',
                  help='Run cloud_to_cloud servers in a separate docker ' +
                  'image. Servers can only be started automatically if ' +
                  '--use_docker option is enabled.',
                  default=[])
argp.add_argument(
    '--override_server',
    action='append',
    type=lambda kv: kv.split('='),
    help=
    'Use servername=HOST:PORT to explicitly specify a server. E.g. csharp=localhost:50000',
    default=[])
# TODO(jtattermusch): the default service_account_key_file only works when --use_docker is used.
argp.add_argument(
    '--service_account_key_file',
    type=str,
    help='The service account key file to use for some auth interop tests.',
    default='/root/service_account/grpc-testing-ebe7c1ac7381.json')
argp.add_argument(
    '--default_service_account',
    type=str,
    help='Default GCE service account email to use for some auth interop tests.',
    default='830293263384-compute@developer.gserviceaccount.com')
argp.add_argument(
    '-t',
    '--travis',
    default=False,
    action='store_const',
    const=True,
    help='When set, indicates that the script is running on CI (= not locally).'
)
argp.add_argument('-v',
                  '--verbose',
                  default=False,
                  action='store_const',
                  const=True)
argp.add_argument(
    '--use_docker',
    default=False,
    action='store_const',
    const=True,
    help='Run all the interop tests under docker. That provides ' +
    'additional isolation and prevents the need to install ' +
    'language specific prerequisites. Only available on Linux.')
argp.add_argument(
    '--allow_flakes',
    default=False,
    action='store_const',
    const=True,
    help=
    'Allow flaky tests to show as passing (re-runs failed tests up to five times)'
)
argp.add_argument('--manual_run',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Prepare things for running interop tests manually. ' +
                  'Preserve docker images after building them and skip '
                  'actually running the tests. Only print commands to run by ' +
                  'hand.')
argp.add_argument(
    '--http2_interop',
    default=False,
    action='store_const',
    const=True,
    help='Enable HTTP/2 client edge case testing. (Bad client, good server)')
argp.add_argument(
    '--http2_server_interop',
    default=False,
    action='store_const',
    const=True,
    help=
    'Enable HTTP/2 server edge case testing. (Includes positive and negative tests'
)
argp.add_argument('--transport_security',
                  choices=_TRANSPORT_SECURITY_OPTIONS,
                  default='tls',
                  type=str,
                  nargs='?',
                  const=True,
                  help='Which transport security mechanism to use.')
argp.add_argument(
    '--custom_credentials_type',
    choices=_CUSTOM_CREDENTIALS_TYPE_OPTIONS,
    default=_CUSTOM_CREDENTIALS_TYPE_OPTIONS,
    nargs='+',
    help=
    'Credential types to test in the cloud_to_prod setup. Default is to test with all creds types possible.'
)
argp.add_argument(
    '--skip_compute_engine_creds',
    default=False,
    action='store_const',
    const=True,
    help='Skip auth tests requiring access to compute engine credentials.')
argp.add_argument(
    '--internal_ci',
    default=False,
    action='store_const',
    const=True,
    help=(
        '(Deprecated, has no effect) Put reports into subdirectories to improve '
        'presentation of results by Internal CI.'))
argp.add_argument('--bq_result_table',
                  default='',
                  type=str,
                  nargs='?',
                  help='Upload test results to a specified BQ table.')
args = argp.parse_args()

servers = set(s for s in itertools.chain.from_iterable(
    _SERVERS if x == 'all' else [x] for x in args.server))
# ALTS servers are only available for certain languages.
if args.transport_security == 'alts':
    servers = servers.intersection(_SERVERS_FOR_ALTS_TEST_CASES)

if args.use_docker:
    if not args.travis:
        print('Seen --use_docker flag, will run interop tests under docker.')
        print('')
        print(
            'IMPORTANT: The changes you are testing need to be locally committed'
        )
        print(
            'because only the committed changes in the current branch will be')
        print('copied to the docker environment.')
        time.sleep(5)

if args.manual_run and not args.use_docker:
    print('--manual_run is only supported with --use_docker option enabled.')
    sys.exit(1)

if not args.use_docker and servers:
    print(
        'Running interop servers is only supported with --use_docker option enabled.'
    )
    sys.exit(1)

# we want to include everything but objc in 'all'
# because objc won't run on non-mac platforms
all_but_objc = set(six.iterkeys(_LANGUAGES)) - set(['objc'])
languages = set(_LANGUAGES[l] for l in itertools.chain.from_iterable(
    all_but_objc if x == 'all' else [x] for x in args.language))
# ALTS interop clients are only available for certain languages.
if args.transport_security == 'alts':
    alts_languages = set(_LANGUAGES[l] for l in _LANGUAGES_FOR_ALTS_TEST_CASES)
    languages = languages.intersection(alts_languages)

languages_http2_clients_for_http2_server_interop = set()
if args.http2_server_interop:
    languages_http2_clients_for_http2_server_interop = set(
        _LANGUAGES[l]
        for l in _LANGUAGES_WITH_HTTP2_CLIENTS_FOR_HTTP2_SERVER_TEST_CASES
        if 'all' in args.language or l in args.language)

http2Interop = Http2Client() if args.http2_interop else None
http2InteropServer = Http2Server() if args.http2_server_interop else None

docker_images = {}
if args.use_docker:
    # languages for which to build docker images
    languages_to_build = set(_LANGUAGES[k]
                             for k in set([str(l) for l in languages] +
                                          [s for s in servers]))
    languages_to_build = languages_to_build | languages_http2_clients_for_http2_server_interop

    if args.http2_interop:
        languages_to_build.add(http2Interop)

    if args.http2_server_interop:
        languages_to_build.add(http2InteropServer)

    build_jobs = []
    for l in languages_to_build:
        if str(l) == 'objc':
            # we don't need to build a docker image for objc
            continue
        job = build_interop_image_jobspec(l)
        docker_images[str(l)] = job.tag
        build_jobs.append(job)

    if build_jobs:
        jobset.message('START',
                       'Building interop docker images.',
                       do_newline=True)
        if args.verbose:
            print('Jobs to run: \n%s\n' % '\n'.join(str(j) for j in build_jobs))

        num_failures, build_resultset = jobset.run(build_jobs,
                                                   newline_on_success=True,
                                                   maxjobs=args.jobs)

        report_utils.render_junit_xml_report(build_resultset,
                                             _DOCKER_BUILD_XML_REPORT)

        if num_failures == 0:
            jobset.message('SUCCESS',
                           'All docker images built successfully.',
                           do_newline=True)
        else:
            jobset.message('FAILED',
                           'Failed to build interop docker images.',
                           do_newline=True)
            for image in six.itervalues(docker_images):
                dockerjob.remove_image(image, skip_nonexistent=True)
            sys.exit(1)

server_manual_cmd_log = [] if args.manual_run else None
client_manual_cmd_log = [] if args.manual_run else None

# Start interop servers.
server_jobs = {}
server_addresses = {}
try:
    for s in servers:
        lang = str(s)
        spec = server_jobspec(_LANGUAGES[lang],
                              docker_images.get(lang),
                              args.transport_security,
                              manual_cmd_log=server_manual_cmd_log)
        if not args.manual_run:
            job = dockerjob.DockerJob(spec)
            server_jobs[lang] = job
            server_addresses[lang] = ('localhost',
                                      job.mapped_port(_DEFAULT_SERVER_PORT))
        else:
            # don't run the server, set server port to a placeholder value
            server_addresses[lang] = ('localhost', '${SERVER_PORT}')

    http2_server_job = None
    if args.http2_server_interop:
        # launch a HTTP2 server emulator that creates edge cases
        lang = str(http2InteropServer)
        spec = server_jobspec(http2InteropServer,
                              docker_images.get(lang),
                              manual_cmd_log=server_manual_cmd_log)
        if not args.manual_run:
            http2_server_job = dockerjob.DockerJob(spec)
            server_jobs[lang] = http2_server_job
        else:
            # don't run the server, set server port to a placeholder value
            server_addresses[lang] = ('localhost', '${SERVER_PORT}')

    jobs = []
    if args.cloud_to_prod:
        if args.transport_security not in ['tls']:
            print('TLS is always enabled for cloud_to_prod scenarios.')
        for server_host_nickname in args.prod_servers:
            for language in languages:
                for test_case in _TEST_CASES:
                    if not test_case in language.unimplemented_test_cases():
                        if not test_case in _SKIP_ADVANCED + _SKIP_COMPRESSION + _SKIP_SPECIAL_STATUS_MESSAGE + _ORCA_TEST_CASES:
                            for transport_security in args.custom_credentials_type:
                                # google_default_credentials not yet supported by all languages
                                if transport_security == 'google_default_credentials' and str(
                                        language) not in [
                                            'c++', 'go', 'java', 'javaokhttp'
                                        ]:
                                    continue
                                # compute_engine_channel_creds not yet supported by all languages
                                if transport_security == 'compute_engine_channel_creds' and str(
                                        language) not in [
                                            'go', 'java', 'javaokhttp'
                                        ]:
                                    continue
                                test_job = cloud_to_prod_jobspec(
                                    language,
                                    test_case,
                                    server_host_nickname,
                                    prod_servers[server_host_nickname],
                                    google_default_creds_use_key_file=args.
                                    google_default_creds_use_key_file,
                                    docker_image=docker_images.get(
                                        str(language)),
                                    manual_cmd_log=client_manual_cmd_log,
                                    service_account_key_file=args.
                                    service_account_key_file,
                                    default_service_account=args.
                                    default_service_account,
                                    transport_security=transport_security)
                                jobs.append(test_job)
            if args.http2_interop:
                for test_case in _HTTP2_TEST_CASES:
                    test_job = cloud_to_prod_jobspec(
                        http2Interop,
                        test_case,
                        server_host_nickname,
                        prod_servers[server_host_nickname],
                        google_default_creds_use_key_file=args.
                        google_default_creds_use_key_file,
                        docker_image=docker_images.get(str(http2Interop)),
                        manual_cmd_log=client_manual_cmd_log,
                        service_account_key_file=args.service_account_key_file,
                        default_service_account=args.default_service_account,
                        transport_security=args.transport_security)
                    jobs.append(test_job)

    if args.cloud_to_prod_auth:
        if args.transport_security not in ['tls']:
            print('TLS is always enabled for cloud_to_prod scenarios.')
        for server_host_nickname in args.prod_servers:
            for language in languages:
                for test_case in _AUTH_TEST_CASES:
                    if (not args.skip_compute_engine_creds or
                            not compute_engine_creds_required(
                                language, test_case)):
                        if not test_case in language.unimplemented_test_cases():
                            if test_case == _GOOGLE_DEFAULT_CREDS_TEST_CASE:
                                transport_security = 'google_default_credentials'
                            elif test_case == _COMPUTE_ENGINE_CHANNEL_CREDS_TEST_CASE:
                                transport_security = 'compute_engine_channel_creds'
                            else:
                                transport_security = 'tls'
                            if transport_security not in args.custom_credentials_type:
                                continue
                            test_job = cloud_to_prod_jobspec(
                                language,
                                test_case,
                                server_host_nickname,
                                prod_servers[server_host_nickname],
                                google_default_creds_use_key_file=args.
                                google_default_creds_use_key_file,
                                docker_image=docker_images.get(str(language)),
                                auth=True,
                                manual_cmd_log=client_manual_cmd_log,
                                service_account_key_file=args.
                                service_account_key_file,
                                default_service_account=args.
                                default_service_account,
                                transport_security=transport_security)
                            jobs.append(test_job)
    for server in args.override_server:
        server_name = server[0]
        (server_host, server_port) = server[1].split(':')
        server_addresses[server_name] = (server_host, server_port)

    for server_name, server_address in list(server_addresses.items()):
        (server_host, server_port) = server_address
        server_language = _LANGUAGES.get(server_name, None)
        skip_server = []  # test cases unimplemented by server
        if server_language:
            skip_server = server_language.unimplemented_test_cases_server()
        for language in languages:
            for test_case in _TEST_CASES:
                if not test_case in language.unimplemented_test_cases():
                    if not test_case in skip_server:
                        test_job = cloud_to_cloud_jobspec(
                            language,
                            test_case,
                            server_name,
                            server_host,
                            server_port,
                            docker_image=docker_images.get(str(language)),
                            transport_security=args.transport_security,
                            manual_cmd_log=client_manual_cmd_log)
                        jobs.append(test_job)

        if args.http2_interop:
            for test_case in _HTTP2_TEST_CASES:
                if server_name == "go":
                    # TODO(carl-mastrangelo): Reenable after https://github.com/grpc/grpc-go/issues/434
                    continue
                test_job = cloud_to_cloud_jobspec(
                    http2Interop,
                    test_case,
                    server_name,
                    server_host,
                    server_port,
                    docker_image=docker_images.get(str(http2Interop)),
                    transport_security=args.transport_security,
                    manual_cmd_log=client_manual_cmd_log)
                jobs.append(test_job)

    if args.http2_server_interop:
        if not args.manual_run:
            http2_server_job.wait_for_healthy(timeout_seconds=600)
        for language in languages_http2_clients_for_http2_server_interop:
            for test_case in set(_HTTP2_SERVER_TEST_CASES) - set(
                    _HTTP2_SERVER_TEST_CASES_THAT_USE_GRPC_CLIENTS):
                offset = sorted(_HTTP2_SERVER_TEST_CASES).index(test_case)
                server_port = _DEFAULT_SERVER_PORT + offset
                if not args.manual_run:
                    server_port = http2_server_job.mapped_port(server_port)
                test_job = cloud_to_cloud_jobspec(
                    language,
                    test_case,
                    str(http2InteropServer),
                    'localhost',
                    server_port,
                    docker_image=docker_images.get(str(language)),
                    manual_cmd_log=client_manual_cmd_log)
                jobs.append(test_job)
        for language in languages:
            # HTTP2_SERVER_TEST_CASES_THAT_USE_GRPC_CLIENTS is a subset of
            # HTTP_SERVER_TEST_CASES, in which clients use their gRPC interop clients rather
            # than specialized http2 clients, reusing existing test implementations.
            # For example, in the "data_frame_padding" test, use language's gRPC
            # interop clients and make them think that they're running "large_unary"
            # test case. This avoids implementing a new test case in each language.
            for test_case in _HTTP2_SERVER_TEST_CASES_THAT_USE_GRPC_CLIENTS:
                if test_case not in language.unimplemented_test_cases():
                    offset = sorted(_HTTP2_SERVER_TEST_CASES).index(test_case)
                    server_port = _DEFAULT_SERVER_PORT + offset
                    if not args.manual_run:
                        server_port = http2_server_job.mapped_port(server_port)
                    if args.transport_security != 'insecure':
                        print(
                            ('Creating grpc client to http2 server test case '
                             'with insecure connection, even though '
                             'args.transport_security is not insecure. Http2 '
                             'test server only supports insecure connections.'))
                    test_job = cloud_to_cloud_jobspec(
                        language,
                        test_case,
                        str(http2InteropServer),
                        'localhost',
                        server_port,
                        docker_image=docker_images.get(str(language)),
                        transport_security='insecure',
                        manual_cmd_log=client_manual_cmd_log)
                    jobs.append(test_job)

    if not jobs:
        print('No jobs to run.')
        for image in six.itervalues(docker_images):
            dockerjob.remove_image(image, skip_nonexistent=True)
        sys.exit(1)

    if args.manual_run:
        print('All tests will skipped --manual_run option is active.')

    if args.verbose:
        print('Jobs to run: \n%s\n' % '\n'.join(str(job) for job in jobs))

    num_failures, resultset = jobset.run(jobs,
                                         newline_on_success=True,
                                         maxjobs=args.jobs,
                                         skip_jobs=args.manual_run)
    if args.bq_result_table and resultset:
        upload_interop_results_to_bq(resultset, args.bq_result_table)
    if num_failures:
        jobset.message('FAILED', 'Some tests failed', do_newline=True)
    else:
        jobset.message('SUCCESS', 'All tests passed', do_newline=True)

    write_cmdlog_maybe(server_manual_cmd_log, 'interop_server_cmds.sh')
    write_cmdlog_maybe(client_manual_cmd_log, 'interop_client_cmds.sh')

    report_utils.render_junit_xml_report(resultset, _TESTS_XML_REPORT)

    for name, job in list(resultset.items()):
        if "http2" in name:
            job[0].http2results = aggregate_http2_results(job[0].message)

    http2_server_test_cases = (_HTTP2_SERVER_TEST_CASES
                               if args.http2_server_interop else [])

    if num_failures:
        sys.exit(1)
    else:
        sys.exit(0)
finally:
    # Check if servers are still running.
    for server, job in list(server_jobs.items()):
        if not job.is_running():
            print('Server "%s" has exited prematurely.' % server)

    dockerjob.finish_jobs([j for j in six.itervalues(server_jobs)])

    for image in six.itervalues(docker_images):
        if not args.manual_run:
            print('Removing docker image %s' % image)
            dockerjob.remove_image(image)
        else:
            print('Preserving docker image: %s' % image)
