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

import argparse
import hyper
import sys

# Utility to healthcheck the http2 server. Used when starting the server to
# verify that the server is live before tests begin.
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--server_host', type=str, default='localhost')
    parser.add_argument('--server_port', type=int, default=8080)
    args = parser.parse_args()
    server_host = args.server_host
    server_port = args.server_port
    conn = hyper.HTTP20Connection('%s:%d' % (server_host, server_port))
    conn.request('POST', '/grpc.testing.TestService/UnaryCall')
    resp = conn.get_response()
    if resp.headers.get('grpc-encoding') is None:
        sys.exit(1)
    else:
        sys.exit(0)
