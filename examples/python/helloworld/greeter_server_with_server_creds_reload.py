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

"""The Python implementation of the GRPC helloworld.Greeter server."""

from concurrent import futures
import time

import grpc

import helloworld_pb2
import helloworld_pb2_grpc

_ONE_DAY_IN_SECONDS = 60 * 60 * 24


class Greeter(helloworld_pb2_grpc.GreeterServicer):

  def SayHello(self, request, context):
    return helloworld_pb2.HelloReply(message='Hello, %s!' % request.name)


with open('key1.pem') as f:
  private_key_1 = f.read()
with open('cert1.pem') as f:
  certificate_chain_1 = f.read()

with open('key2.pem') as f:
  private_key_2 = f.read()
with open('cert2.pem') as f:
  certificate_chain_2 = f.read()


client_num = 0

def get_server_credentials_cb():
  global client_num
  client_num += 1

  if client_num != 3:
    return False, None
  else:
    return True, grpc.ssl_server_credentials(
      [(private_key_2, certificate_chain_2)])

def serve():

  if 0:
    server_credentials = grpc.ssl_server_credentials(
      [(private_key_1, certificate_chain_1)])
  else:
    server_credentials = grpc.ssl_server_credentials(
      [(private_key_1, certificate_chain_1)],
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
