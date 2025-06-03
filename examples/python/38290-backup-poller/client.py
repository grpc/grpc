# Copyright 2025 gRPC authors.
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


import time
import traceback
import grpc

import load_protos
import server_pb2
import server_pb2_grpc


def main():
    channel = grpc.insecure_channel("127.0.0.1:50051")
    stub = server_pb2_grpc.ServerStub(channel)
    i: int = 0
    while True:
        try:
            i += 1
            resp = stub.Method(server_pb2.Message(id=str(i)))
            print(f"Received Message: id={resp.id}")
        except grpc.RpcError:
            print(traceback.format_exc())
        except Exception as e:
            print(e)

        time.sleep(1)


if __name__ == "__main__":
    main()
