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

"""The Python implementation of the GRPC interoperability test server."""

import argparse
import logging
import time

from grpc.beta import implementations

from tests.interop import methods
from tests.interop import resources
from tests.interop import test_pb2

_ONE_DAY_IN_SECONDS = 60 * 60 * 24


def serve():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      '--port', help='the port on which to serve', type=int)
  parser.add_argument(
      '--use_tls', help='require a secure connection',
      default=False, type=resources.parse_bool)
  args = parser.parse_args()

  server = test_pb2.beta_create_TestService_server(methods.TestService())
  if args.use_tls:
    private_key = resources.private_key()
    certificate_chain = resources.certificate_chain()
    credentials = implementations.ssl_server_credentials(
        [(private_key, certificate_chain)])
    server.add_secure_port('[::]:{}'.format(args.port), credentials)
  else:
    server.add_insecure_port('[::]:{}'.format(args.port))

  server.start()
  logging.info('Server serving.')
  try:
    while True:
      time.sleep(_ONE_DAY_IN_SECONDS)
  except BaseException as e:
    logging.info('Caught exception "%s"; stopping server...', e)
    server.stop(0)
    logging.info('Server stopped; exiting.')

if __name__ == '__main__':
  serve()
