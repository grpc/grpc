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

"""The Python implementation of the GRPC helloworld.Greeter client."""

from __future__ import print_function

import time

import grpc

import helloworld_pb2
import helloworld_pb2_grpc

# will switch between these two server-verifying certs
server_cas_pem = [
  open('cert_hier_1/certs/ca.cert.pem').read(),
  open('cert_hier_2/certs/ca.cert.pem').read(),
]

my_key_pem = open('cert_hier_1/intermediate/private/client.key.pem').read()
my_cert_pem = '\n'.join([
  open('cert_hier_1/intermediate/certs/client.cert.pem').read(),
  open('cert_hier_1/intermediate/certs/intermediate.cert.pem').read(),
])


def run():

  server_ca_idx = 0
  for i in range(5):
    print('-'*50)
    print('using root ca cert {}'.format(server_ca_idx+1))
    server_ca_pem = server_cas_pem[server_ca_idx] # for verifying server
    credentials = grpc.ssl_channel_credentials(
      root_certificates=server_ca_pem,
      private_key=my_key_pem, certificate_chain=my_cert_pem,
    )
    channel = grpc.secure_channel('localhost:50051', credentials)
    stub = helloworld_pb2_grpc.GreeterStub(channel)
    try:
      response = stub.SayHello(helloworld_pb2.HelloRequest(name='you'))
      print("Greeter client received: " + response.message)
    except:
      print("maybe handshake fails? switching root ca cert")
      server_ca_idx += 1
      pass
    time.sleep(2)
    print


if __name__ == '__main__':
  run()
