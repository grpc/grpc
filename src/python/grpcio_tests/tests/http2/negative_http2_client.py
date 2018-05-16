# Copyright 2016 gRPC authors.
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
"""The Python client used to test negative http2 conditions."""

import argparse

import grpc
import time
from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import messages_pb2


def _validate_payload_type_and_length(response, expected_type, expected_length):
    if response.payload.type is not expected_type:
        raise ValueError('expected payload type %s, got %s' %
                         (expected_type, type(response.payload.type)))
    elif len(response.payload.body) != expected_length:
        raise ValueError('expected payload body size %d, got %d' %
                         (expected_length, len(response.payload.body)))


def _expect_status_code(call, expected_code):
    if call.code() != expected_code:
        raise ValueError('expected code %s, got %s' % (expected_code,
                                                       call.code()))


def _expect_status_details(call, expected_details):
    if call.details() != expected_details:
        raise ValueError('expected message %s, got %s' % (expected_details,
                                                          call.details()))


def _validate_status_code_and_details(call, expected_code, expected_details):
    _expect_status_code(call, expected_code)
    _expect_status_details(call, expected_details)


# common requests
_REQUEST_SIZE = 314159
_RESPONSE_SIZE = 271828

_SIMPLE_REQUEST = messages_pb2.SimpleRequest(
    response_type=messages_pb2.COMPRESSABLE,
    response_size=_RESPONSE_SIZE,
    payload=messages_pb2.Payload(body=b'\x00' * _REQUEST_SIZE))


def _goaway(stub):
    first_response = stub.UnaryCall(_SIMPLE_REQUEST)
    _validate_payload_type_and_length(first_response, messages_pb2.COMPRESSABLE,
                                      _RESPONSE_SIZE)
    time.sleep(1)
    second_response = stub.UnaryCall(_SIMPLE_REQUEST)
    _validate_payload_type_and_length(second_response,
                                      messages_pb2.COMPRESSABLE, _RESPONSE_SIZE)


def _rst_after_header(stub):
    resp_future = stub.UnaryCall.future(_SIMPLE_REQUEST)
    _validate_status_code_and_details(resp_future, grpc.StatusCode.INTERNAL,
                                      "Received RST_STREAM with error code 0")


def _rst_during_data(stub):
    resp_future = stub.UnaryCall.future(_SIMPLE_REQUEST)
    _validate_status_code_and_details(resp_future, grpc.StatusCode.INTERNAL,
                                      "Received RST_STREAM with error code 0")


def _rst_after_data(stub):
    resp_future = stub.UnaryCall.future(_SIMPLE_REQUEST)
    _validate_status_code_and_details(resp_future, grpc.StatusCode.INTERNAL,
                                      "Received RST_STREAM with error code 0")


def _ping(stub):
    response = stub.UnaryCall(_SIMPLE_REQUEST)
    _validate_payload_type_and_length(response, messages_pb2.COMPRESSABLE,
                                      _RESPONSE_SIZE)


def _max_streams(stub):
    # send one req to ensure server sets MAX_STREAMS
    response = stub.UnaryCall(_SIMPLE_REQUEST)
    _validate_payload_type_and_length(response, messages_pb2.COMPRESSABLE,
                                      _RESPONSE_SIZE)

    # give the streams a workout
    futures = []
    for _ in range(15):
        futures.append(stub.UnaryCall.future(_SIMPLE_REQUEST))
    for future in futures:
        _validate_payload_type_and_length(
            future.result(), messages_pb2.COMPRESSABLE, _RESPONSE_SIZE)


def _run_test_case(test_case, stub):
    if test_case == 'goaway':
        _goaway(stub)
    elif test_case == 'rst_after_header':
        _rst_after_header(stub)
    elif test_case == 'rst_during_data':
        _rst_during_data(stub)
    elif test_case == 'rst_after_data':
        _rst_after_data(stub)
    elif test_case == 'ping':
        _ping(stub)
    elif test_case == 'max_streams':
        _max_streams(stub)
    else:
        raise ValueError("Invalid test case: %s" % test_case)


def _args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--server_host',
        help='the host to which to connect',
        type=str,
        default="127.0.0.1")
    parser.add_argument(
        '--server_port',
        help='the port to which to connect',
        type=int,
        default="8080")
    parser.add_argument(
        '--test_case',
        help='the test case to execute',
        type=str,
        default="goaway")
    return parser.parse_args()


def _stub(server_host, server_port):
    target = '{}:{}'.format(server_host, server_port)
    channel = grpc.insecure_channel(target)
    grpc.channel_ready_future(channel).result()
    return test_pb2_grpc.TestServiceStub(channel)


def main():
    args = _args()
    stub = _stub(args.server_host, args.server_port)
    _run_test_case(args.test_case, stub)


if __name__ == '__main__':
    main()
