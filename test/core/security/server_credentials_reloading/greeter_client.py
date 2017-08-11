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

from __future__ import print_function

import sys

import grpc

import helloworld_pb2
import helloworld_pb2_grpc


server_ca_pem = open(sys.argv[1]).read()
my_key_pem = open(sys.argv[2]).read()
my_cert_pem = open(sys.argv[3]).read()


def run():
  credentials = grpc.ssl_channel_credentials(
    root_certificates=server_ca_pem,
    private_key=my_key_pem, certificate_chain=my_cert_pem,
  )
  channel = grpc.secure_channel('localhost:50051', credentials)
  stub = helloworld_pb2_grpc.GreeterStub(channel)
  response = stub.SayHello(helloworld_pb2.HelloRequest(name='you'))
  print("Greeter client received: " + response.message)
  return

if __name__ == '__main__':
  run()
