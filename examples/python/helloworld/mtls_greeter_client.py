# Copyright 2020 gRPC authors.
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
"""The Python implementation of the GRPC helloworld.Greeter client with mTLS."""

from __future__ import print_function
import argparse
import logging
import grpc
import helloworld_pb2
import helloworld_pb2_grpc


def run(server_addr, root_cert, client_key, client_pem):
    with open(root_cert, 'rb') as f:
        trusted_certs = f.read()
    with open(client_key, 'rb') as f:
        private_key = f.read()
    with open(client_pem, 'rb') as f:
        certificate_chain = f.read()

    channel_creds = grpc.ssl_channel_credentials(trusted_certs, private_key, certificate_chain)
    with grpc.secure_channel(server_addr, channel_creds) as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        response = stub.SayHello(helloworld_pb2.HelloRequest(name='world'))
    print("Greeter client received: " + response.message)


if __name__ == '__main__':
    logging.basicConfig()
    parser = argparse.ArgumentParser()
    parser.add_argument('--server_address', default='localhost:50051', help='address of the server')
    parser.add_argument('--server_root_cert_pem_path', default='testdata/ca.cert', help='path to root X509 certificate')
    parser.add_argument('--client_key_pem_path', default='testdata/client.key', help='path to client\'s private key')
    parser.add_argument('--client_cert_pem_path', default='testdata/client.pem',
                        help='path to client\'s X509 certificate')
    args = vars(parser.parse_args())
    run(args['server_address'], args['server_root_cert_pem_path'], args['client_key_pem_path'],
        args['client_cert_pem_path'])
