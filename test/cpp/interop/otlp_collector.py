#
#
# Copyright 2026 gRPC authors.
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
#
#

import argparse
import json
import os
import threading
import time
from concurrent import futures
import grpc
from google.protobuf import json_format

from opentelemetry.proto.collector.trace.v1 import trace_service_pb2
from opentelemetry.proto.collector.trace.v1 import trace_service_pb2_grpc

class TraceServiceServicer(trace_service_pb2_grpc.TraceServiceServicer):
    def __init__(self, file_path):
        self._file_path = file_path
        self._requests = []
        self._lock = threading.Lock()

    def Export(self, request, context):
        req_dict = json_format.MessageToDict(request, preserving_proto_field_name=True)
        with self._lock:
            self._requests.append(req_dict)
            tmp_file = self._file_path + ".tmp"
            with open(tmp_file, "w") as f:
                json.dump(self._requests, f, indent=2)
                f.flush()
            os.replace(tmp_file, self._file_path)

        return trace_service_pb2.ExportTraceServiceResponse()

def serve():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--file", type=str, required=True)
    args = parser.parse_args()

    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    trace_service_pb2_grpc.add_TraceServiceServicer_to_server(
        TraceServiceServicer(args.file), server
    )
    server.add_insecure_port(f"[::]:{args.port}")
    server.start()
    print(f"OTLP Collector listening on port {args.port}...")
    try:
        while True:
            time.sleep(86400)
    except KeyboardInterrupt:
        server.stop(0)

if __name__ == "__main__":
    serve()
