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

from grpc_test.beta import test_utilities

from grpc_interop import methods
from grpc_interop import resources
from grpc_interop import test_pb2

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

def _oauth_access_token(args):
  credentials = oauth2client_client.GoogleCredentials.get_application_default()
  scoped_credentials = credentials.create_scoped([args.oauth_scope])
  return scoped_credentials.get_access_token().access_token

def _stub(args):
  if args.oauth_scope:
    if args.test_case == 'oauth2_auth_token':
      # TODO(jtattermusch): This testcase sets the auth metadata key-value
      # manually, which also means that the user would need to do the same
      # thing every time he/she would like to use and out of band oauth token.
      # The transformer function that produces the metadata key-value from
      # the access token should be provided by gRPC auth library.
      access_token = _oauth_access_token(args)
      metadata_transformer = lambda x: [
          ('authorization', 'Bearer %s' % access_token)]
    else:
      metadata_transformer = lambda x: [
          ('authorization', 'Bearer %s' % _oauth_access_token(args))]
  else:
    metadata_transformer = lambda x: []
  if args.use_tls:
    if args.use_test_ca:
      root_certificates = resources.test_root_certificates()
    else:
      root_certificates = resources.prod_root_certificates()

    channel = test_utilities.not_really_secure_channel(
        args.server_host, args.server_port,
        implementations.ssl_client_credentials(root_certificates, None, None),
        args.server_host_override)
    stub = test_pb2.beta_create_TestService_stub(
        channel, metadata_transformer=metadata_transformer)
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


def _test_interoperability():
  args = _args()
  stub = _stub(args)
  test_case = _test_case_from_arg(args.test_case)
  test_case.test_interoperability(stub, args)


if __name__ == '__main__':
  _test_interoperability()
