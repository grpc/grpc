# Copyright 2020 The gRPC Authors
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
import threading
from typing import Iterator
from concurrent.futures import ThreadPoolExecutor

import grpc

import phone_pb2
import phone_pb2_grpc


class CallMaker:

    def __init__(self, executor: ThreadPoolExecutor, channel: grpc.Channel,
                 phone_number: str) -> None:
        self._executor = executor
        self._channel = channel
        self._stub = phone_pb2_grpc.PhoneStub(self._channel)
        self._phone_number = phone_number
        self._session_id = None
        self._audio_session_link = None
        self._call_state = None
        self._peer_responded = threading.Event()
        self._call_finished = threading.Event()
        self._consumer_future = None

    def _response_watcher(
            self,
            response_iterator: Iterator[phone_pb2.StreamCallResponse]) -> None:
        try:
            for response in response_iterator:
                # NOTE: All fields in Proto3 are optional. This is the recommended way
                # to check if a field is present or not, or to exam which one-of field is
                # fulfilled by this message.
                if response.HasField("call_info"):
                    self._on_call_info(response.call_info)
                elif response.HasField("call_state"):
                    self._on_call_state(response.call_state.state)
                else:
                    raise RuntimeError(
                        "Received StreamCallResponse without call_info and call_state"
                    )
        except Exception as e:
            self._peer_responded.set()
            raise

    def _on_call_info(self, call_info: phone_pb2.CallInfo) -> None:
        self._session_id = call_info.session_id
        self._audio_session_link = call_info.media

    def _on_call_state(self, call_state: phone_pb2.CallState.State) -> None:
        logging.info("Call toward [%s] enters [%s] state", self._phone_number,
                     phone_pb2.CallState.State.Name(call_state))
        self._call_state = call_state
        if call_state is phone_pb2.CallState.State.ACTIVE:
            self._peer_responded.set()
        if call_state is phone_pb2.CallState.State.ENDED:
            self._peer_responded.set()
            self._call_finished.set()

    def call(self) -> None:
        request = phone_pb2.StreamCallRequest()
        request.phone_number = self._phone_number
        response_iterator = self._stub.StreamCall(iter((request,)))
        # Instead of consuming the response on current thread, spawn a consumption thread.
        self._consumer_future = self._executor.submit(self._response_watcher,
                                                      response_iterator)

    def wait_peer(self) -> None:
        logging.info("Waiting for peer to connect [%s]...", self._phone_number)
        self._peer_responded.wait(timeout=None)
        if self._consumer_future.done():
            # If the future raises, forwards the exception here
            self._consumer_future.result()
        return self._call_state is phone_pb2.CallState.State.ACTIVE

    def audio_session(self) -> None:
        assert self._audio_session_link is not None
        logging.info("Consuming audio resource [%s]", self._audio_session_link)
        self._call_finished.wait(timeout=None)
        logging.info("Audio session finished [%s]", self._audio_session_link)


def process_call(executor: ThreadPoolExecutor, channel: grpc.Channel,
                 phone_number: str) -> None:
    call_maker = CallMaker(executor, channel, phone_number)
    call_maker.call()
    if call_maker.wait_peer():
        call_maker.audio_session()
        logging.info("Call finished!")
    else:
        logging.info("Call failed: peer didn't answer")


def run():
    executor = ThreadPoolExecutor()
    with grpc.insecure_channel("localhost:50051") as channel:
        future = executor.submit(process_call, executor, channel,
                                 "555-0100-XXXX")
        future.result()


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    run()
