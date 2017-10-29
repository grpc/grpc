# Copyright 2017 gRPC authors.
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

from concurrent import futures
import time
import sys
import os

import grpc

import helloworld_pb2
import helloworld_pb2_grpc

_ONE_DAY_IN_SECONDS = 60 * 60 * 24


DIR = os.path.abspath(os.path.dirname(__file__))

def _get_abs_path(path):
    return DIR + '/' + path


class Greeter(helloworld_pb2_grpc.GreeterServicer):

  def SayHello(self, request, context):
    return helloworld_pb2.HelloReply(message='Hello, %s' % request.name)


class CertConfigFetcher(object):
  def __init__(self, client_ca_1_pem, my_key_1_pem, my_cert_1_pem,
               client_ca_2_pem, my_key_2_pem, my_cert_2_pem,
               switch_cert_on_client_num):
    self.client_num = -1
    self.client_ca_2_pem = client_ca_2_pem
    self.my_key_2_pem = my_key_2_pem
    self.my_cert_2_pem = my_cert_2_pem
    self.switch_cert_on_client_num = switch_cert_on_client_num
    self.cert_config = grpc.ssl_server_certificate_config(
        [(my_key_1_pem, my_cert_1_pem)],
        root_certificates=client_ca_1_pem,
      )
    # we simulate cb failures at 2 and 3
    assert self.switch_cert_on_client_num > 3

  def __call__(self):
    self.client_num += 1
    if self.client_num == 0:
      # this is actually during server initialzation
      return self.cert_config
    else:
      print 'client_num', self.client_num

      if self.client_num in (2, 3):
        raise ValueError('just an error for fun, should not affect the test')

      if self.client_num != self.switch_cert_on_client_num:
        return None
      else:
        # do the switch
        return grpc.ssl_server_certificate_config(
          [(self.my_key_2_pem, self.my_cert_2_pem)],
          root_certificates=self.client_ca_2_pem,
        )


def serve():

  # server will switch its cert at this client number (1-based)
  switch_cert_on_client_num = int(sys.argv[1])
  require_client_auth = (sys.argv[2] == 'True')

  # for verifying clients
  client_ca_1_pem = open(_get_abs_path('cert_hier_1/certs/ca.cert.pem')).read()
  client_ca_2_pem = open(_get_abs_path('cert_hier_2/certs/ca.cert.pem')).read()

  my_key_1_pem = \
    open(_get_abs_path('cert_hier_1/intermediate/private/localhost-1.key.pem')).read()
  my_cert_1_pem = '\n'.join([
    open(_get_abs_path('cert_hier_1/intermediate/certs/localhost-1.cert.pem')).read(),
    open(_get_abs_path('cert_hier_1/intermediate/certs/intermediate.cert.pem')).read(),
  ])

  my_key_2_pem = \
    open(_get_abs_path('cert_hier_2/intermediate/private/localhost-1.key.pem')).read()
  my_cert_2_pem = '\n'.join([
    open(_get_abs_path('cert_hier_2/intermediate/certs/localhost-1.cert.pem')).read(),
    open(_get_abs_path('cert_hier_2/intermediate/certs/intermediate.cert.pem')).read(),
  ])

  ## we will start with using cert/key1 and trusting ca2 for client
  ## auth; then after switch, we will use cert/key2 and trust ca1 for
  ## client auth

  cert_config_fetcher = CertConfigFetcher(client_ca_2_pem, my_key_1_pem, my_cert_1_pem,
                                          client_ca_1_pem, my_key_2_pem, my_cert_2_pem,
                                          switch_cert_on_client_num)

  server_credentials = grpc.ssl_server_credentials_with_cert_config_fetcher(
    cert_config_fetcher, require_client_auth=require_client_auth)

  server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
  helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
  server.add_secure_port('[::]:50051', server_credentials)
  server.start()
  try:
    while True:
      time.sleep(_ONE_DAY_IN_SECONDS)
  except KeyboardInterrupt:
    server.stop(0)

if __name__ == '__main__':
  serve()
