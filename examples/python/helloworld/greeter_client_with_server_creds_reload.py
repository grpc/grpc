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

"""The Python implementation of the GRPC helloworld.Greeter client."""

from __future__ import print_function

import time

import grpc

import helloworld_pb2
import helloworld_pb2_grpc

cacerts = []
with open('cert1.pem') as fp:
  cacerts.append(fp.read())
with open('cert2.pem') as fp:
  cacerts.append(fp.read())

def run():

  cacert_idx = 0
  for i in range(5):
    print('-'*50)
    print( 'using ca cert{}.pem'.format(cacert_idx+1))
    cacert = cacerts[cacert_idx]
    credentials = grpc.ssl_channel_credentials(root_certificates=cacert)
    channel = grpc.secure_channel('localhost:50051', credentials)
    stub = helloworld_pb2_grpc.GreeterStub(channel)
    try:
      response = stub.SayHello(helloworld_pb2.HelloRequest(name='you'))
      print("Greeter client received: " + response.message)
    except:
      print("handshake fails? switching ca cert")
      cacert_idx += 1
      pass
    time.sleep(2)
    print


if __name__ == '__main__':
  run()
