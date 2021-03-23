# Copyright 2020 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-1.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""The Python implementation of the GRPC helloworld.Greeter server with mTLS."""

from concurrent import futures
import argparse
import logging
import grpc
import helloworld_pb2
import helloworld_pb2_grpc


class Greeter(helloworld_pb2_grpc.GreeterServicer):

    def SayHello(self, request, context):
        print('Received: ' + request.name)
        return helloworld_pb2.HelloReply(message='Hello, %s!' % request.name)


def serve(port, root_cert, server_key, server_pem):
    with open(root_cert, 'rb') as f:
        trusted_certs = f.read()
    with open(server_key, 'rb') as f:
        private_key = f.read()
    with open(server_pem, 'rb') as f:
        certificate_chain = f.read()

    server_creds = grpc.ssl_server_credentials(((private_key, certificate_chain,),), trusted_certs, True)
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    server.add_secure_port('[::]:'+port, server_creds)
    server.start()
    print('Greeter server is listening on port ' + port)
    server.wait_for_termination()


if __name__ == '__main__':
    logging.basicConfig()
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default='50051', help='port number to use for connection')
    parser.add_argument('--client_root_cert_pem_path', default='testdata/ca.cert', help='path to root X509 certificate')
    parser.add_argument('--server_key_pem_path', default='testdata/service.key', help='path to server\'s private key')
    parser.add_argument('--server_cert_pem_path', default='testdata/service.pem',
                        help='path to server\'s X509 certificate')
    args = vars(parser.parse_args())
    serve(args['port'], args['client_root_cert_pem_path'], args['server_key_pem_path'], args['server_cert_pem_path'])
