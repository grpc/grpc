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

from grpc.beta import implementations
from src.proto.grpc.testing import test_pb2

from tests.interop import methods
from tests.interop import resources
from tests.unit.beta import test_utilities

_ONE_DAY_IN_SECONDS = 60 * 60 * 24


def _args():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      '--server_host', help='the host to which to connect', type=str)
  parser.add_argument(
      '--server_port', help='the port to which to connect', type=int)
  parser.add_argument(
      '--test_case', help='the test case to execute', type=str)
  parser.add_argument(
      '--use_tls', help='require a secure connection', default=False,
      type=resources.parse_bool)
  parser.add_argument(
      '--use_test_ca', help='replace platform root CAs with ca.pem',
      default=False, type=resources.parse_bool)
  parser.add_argument(
      '--server_host_override',
      help='the server host to which to claim to connect', type=str)
  parser.add_argument('--oauth_scope', help='scope for OAuth tokens', type=str)
  parser.add_argument(
      '--default_service_account',
      help='email address of the default service account', type=str)
  return parser.parse_args()


def _stub(args):
  if args.test_case == 'oauth2_auth_token':
    creds = oauth2client_client.GoogleCredentials.get_application_default()
    scoped_creds = creds.create_scoped([args.oauth_scope])
    access_token = scoped_creds.get_access_token().access_token
    call_creds = implementations.access_token_call_credentials(access_token)
  elif args.test_case == 'compute_engine_creds':
    creds = oauth2client_client.GoogleCredentials.get_application_default()
    scoped_creds = creds.create_scoped([args.oauth_scope])
    call_creds = implementations.google_call_credentials(scoped_creds)
  elif args.test_case == 'jwt_token_creds':
    creds = oauth2client_client.GoogleCredentials.get_application_default()
    call_creds = implementations.google_call_credentials(creds)
  else:
    call_creds = None
  if args.use_tls:
    if args.use_test_ca:
      root_certificates = resources.test_root_certificates()
    else:
      root_certificates = None  # will load default roots.

    channel_creds = implementations.ssl_channel_credentials(root_certificates)
    if call_creds is not None:
      channel_creds = implementations.composite_channel_credentials(
          channel_creds, call_creds)

    channel = test_utilities.not_really_secure_channel(
        args.server_host, args.server_port, channel_creds,
        args.server_host_override)
    stub = test_pb2.beta_create_TestService_stub(channel)
  else:
    channel = implementations.insecure_channel(
        args.server_host, args.server_port)
    stub = test_pb2.beta_create_TestService_stub(channel)
  return stub


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
