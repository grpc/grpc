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

from concurrent import futures
import logging
import signal
import time
from types import FrameType
from typing import Any, Callable
import grpc

import load_protos
import server_pb2
import server_pb2_grpc

_SignalNum = int | signal.Signals
_SignalFrame = FrameType | None
# _SignalHandler = Callable[[int, _SignalFrame], Any] | _SignalNum | None


class ServerSvc(server_pb2_grpc.ServerServicer):
    def __init__(self, server_id) -> None:
        super().__init__()
        self.server_i = server_id

    def Method(self, request: server_pb2.Message, context):
        request.id = f"{request.id}-server#{self.server_i}"
        return request


class MyServer:
    server: grpc.Server | None = None
    server_id: int = 1

    _handling_sigint: bool = False

    def __init__(self):
        self.server = None
        self.server_id = 0

    def register_signals(self):
        signal.signal(signal.SIGINT, self.handle_sigint)

    def handle_sigint(self, signalnum: _SignalNum, frame: _SignalFrame) -> None:
        print(flush=True)
        if self._handling_sigint:
            logging.info("Ctrl+C pressed twice, exiting.")
        elif self.server is not None:
            delay_sec = 2
            logging.info(
                "Caught Ctrl+C. Server will be stopped in %d seconds."
                " Press Ctrl+C again to abort.",
                delay_sec,
            )
            self._handling_sigint = True
            # Sleep for a few seconds to allow second Ctrl-C before the stop.
            time.sleep(delay_sec)
            self.restart()
            return

        # Remove the sigint handler.
        self._handling_sigint = False
        logging.info("Full stop")
        raise SystemExit

    def restart(self):
        if self.server is None:
            raise RuntimeError("Server is None")

        logging.info("Stopping the server")
        self.server.stop(False)

        delay_sec = 2
        logging.info("Server will be restarted in %d seconds.", delay_sec)
        time.sleep(delay_sec)

        self._handling_sigint = False
        self.serve()

    def serve(self):
        self.server_id += 1
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=5))
        self.server = server

        server_pb2_grpc.add_ServerServicer_to_server(ServerSvc(self.server_id), server)
        listen_addr = "127.0.0.1:50051"
        server.add_insecure_port(listen_addr)

        server.start()
        logging.info(f"Starting server #{self.server_id} on {listen_addr}")
        server.wait_for_termination()

    def run(self):
        self.register_signals()
        self.serve()


def main():
    time_format = "%Y-%m-%d %H:%M:%S"
    log_format = "%(asctime)s %(message)s"
    logging.basicConfig(level=logging.INFO, format=log_format, datefmt=time_format)
    MyServer().run()


if __name__ == "__main__":
    main()
