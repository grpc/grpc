# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""The Python implementation of the GRPC interoperability test client."""

import argparse
from oauth2client import client as oauth2client_client

import grpc
from grpc.beta import implementations
from src.proto.grpc.testing import test_pb2

from tests.interop import methods
from tests.interop import resources


def _args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--server_host',
        help='the host to which to connect',
        type=str,
        default="localhost")
    parser.add_argument(
        '--server_port', help='the port to which to connect', type=int)
    parser.add_argument(
        '--test_case',
        help='the test case to execute',
        type=str,
        default="large_unary")
    parser.add_argument(
        '--use_tls',
        help='require a secure connection',
        default=False,
        type=resources.parse_bool)
    parser.add_argument(
        '--use_test_ca',
        help='replace platform root CAs with ca.pem',
        default=False,
        type=resources.parse_bool)
    parser.add_argument(
        '--server_host_override',
        default="foo.test.google.fr",
        help='the server host to which to claim to connect',
        type=str)
    parser.add_argument(
        '--oauth_scope', help='scope for OAuth tokens', type=str)
    parser.add_argument(
        '--default_service_account',
        help='email address of the default service account',
        type=str)
    return parser.parse_args()


def _application_default_credentials():
    return oauth2client_client.GoogleCredentials.get_application_default()


def _stub(args):
    target = '{}:{}'.format(args.server_host, args.server_port)
    if args.test_case == 'oauth2_auth_token':
        google_credentials = _application_default_credentials()
        scoped_credentials = google_credentials.create_scoped(
            [args.oauth_scope])
        access_token = scoped_credentials.get_access_token().access_token
        call_credentials = grpc.access_token_call_credentials(access_token)
    elif args.test_case == 'compute_engine_creds':
        google_credentials = _application_default_credentials()
        scoped_credentials = google_credentials.create_scoped(
            [args.oauth_scope])
        # TODO(https://github.com/grpc/grpc/issues/6799): Eliminate this last
        # remaining use of the Beta API.
        call_credentials = implementations.google_call_credentials(
            scoped_credentials)
    elif args.test_case == 'jwt_token_creds':
        google_credentials = _application_default_credentials()
        # TODO(https://github.com/grpc/grpc/issues/6799): Eliminate this last
        # remaining use of the Beta API.
        call_credentials = implementations.google_call_credentials(
            google_credentials)
    else:
        call_credentials = None
    if args.use_tls:
        if args.use_test_ca:
            root_certificates = resources.test_root_certificates()
        else:
            root_certificates = None  # will load default roots.

        channel_credentials = grpc.ssl_channel_credentials(root_certificates)
        if call_credentials is not None:
            channel_credentials = grpc.composite_channel_credentials(
                channel_credentials, call_credentials)

        channel = grpc.secure_channel(target, channel_credentials, (
            ('grpc.ssl_target_name_override', args.server_host_override,),))
    else:
        channel = grpc.insecure_channel(target)
    if args.test_case == "unimplemented_service":
        return test_pb2.UnimplementedServiceStub(channel)
    else:
        return test_pb2.TestServiceStub(channel)


def _test_case_from_arg(test_case_arg):
    for test_case in methods.TestCase:
        if test_case_arg == test_case.value:
            return test_case
    else:
        raise ValueError('No test case "%s"!' % test_case_arg)


def test_interoperability():
    args = _args()
    stub = _stub(args)
    test_case = _test_case_from_arg(args.test_case)
    test_case.test_interoperability(stub, args)


if __name__ == '__main__':
    test_interoperability()
