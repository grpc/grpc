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

import logging
import signal
import time
from concurrent import futures
from types import FrameType
from typing import Final

import grpc

import load_protos
import server_pb2
import server_pb2_grpc

_SignalNum = int | signal.Signals
_SignalFrame = FrameType | None


class ServerSvc(server_pb2_grpc.ServerServicer):
    def __init__(self, server_id) -> None:
        super().__init__()
        self.server_i = server_id

    def Method(self, request: server_pb2.Message, context):
        request.id = f"{request.id}-server#{self.server_i}"
        return request


class GrpcTestApp:
    LISTEN_ADDR: Final[str] = "127.0.0.1:50051"

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

        logging.info("[SERVER] Stopping the server #%s", self.server_id)
        self.server.stop(False)

        delay_sec = 2
        logging.info("[SERVER] Server will be restarted in %d seconds.", delay_sec)
        time.sleep(delay_sec)

        self._handling_sigint = False
        server = self.serve()
        # server.wait_for_termination()

    def serve(self):
        self.server_id += 1
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=5))
        self.server = server

        server_pb2_grpc.add_ServerServicer_to_server(ServerSvc(self.server_id), server)
        server.add_insecure_port(self.LISTEN_ADDR)

        logging.info(
            "[SERVER] Starting server #%s on %s", self.server_id, self.LISTEN_ADDR
        )
        server.start()
        return server

    def start_client(self):
        channel = grpc.insecure_channel(self.LISTEN_ADDR)
        stub = server_pb2_grpc.ServerStub(channel)
        i: int = 0
        while True:
            try:
                i += 1
                resp = stub.Method(server_pb2.Message(id=str(i)))
                logging.info(f"[CLIENT] Received Message: id={resp.id}")
            except grpc.RpcError as e:
                logging.error("[CLIENT] %r", e)

            time.sleep(1)
            if i == 10:
                self.restart()

    def run(self):
        self.register_signals()
        server = self.serve()
        self.start_client()
        # server.wait_for_termination()


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    GrpcTestApp().run()


if __name__ == "__main__":
    main()
