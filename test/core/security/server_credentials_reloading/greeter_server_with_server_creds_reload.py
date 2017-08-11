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

"""The Python implementation of the GRPC helloworld.Greeter server."""

from concurrent import futures
import time
import sys

import grpc

import helloworld_pb2
import helloworld_pb2_grpc

_ONE_DAY_IN_SECONDS = 60 * 60 * 24


class Greeter(helloworld_pb2_grpc.GreeterServicer):

  def SayHello(self, request, context):
    return helloworld_pb2.HelloReply(message='Hello, %s' % request.name)


# server will switch its credentials at this client number (1-based)
switch_creds_on_client_num = int(sys.argv[1])

# for verifying clients
client_ca_pem = open('cert_hier_1/certs/ca.cert.pem').read()

my_key_1_pem = \
  open('cert_hier_1/intermediate/private/localhost-1.key.pem').read()
my_cert_1_pem = '\n'.join([
  open('cert_hier_1/intermediate/certs/localhost-1.cert.pem').read(),
  open('cert_hier_1/intermediate/certs/intermediate.cert.pem').read(),
])

my_key_2_pem = \
  open('cert_hier_2/intermediate/private/localhost-1.key.pem').read()
my_cert_2_pem = '\n'.join([
  open('cert_hier_2/intermediate/certs/localhost-1.cert.pem').read(),
  open('cert_hier_2/intermediate/certs/intermediate.cert.pem').read(),
])

client_num = 0

def get_server_credentials_cb():
  global client_num
  client_num += 1
  print 'client_num', client_num

  if client_num != switch_creds_on_client_num:
    return False, None
  else:
    # switch credentials
    return True, grpc.ssl_server_credentials(
      [(my_key_2_pem, my_cert_2_pem)],
      require_client_auth=True,
      root_certificates=client_ca_pem,
    )


def serve():

  server_credentials = grpc.ssl_server_credentials(
    [(my_key_1_pem, my_cert_1_pem)],
    require_client_auth=True,
    root_certificates=client_ca_pem,
    get_server_credentials_cb=get_server_credentials_cb,
  )

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
