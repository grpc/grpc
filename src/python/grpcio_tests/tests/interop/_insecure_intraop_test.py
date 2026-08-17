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
"""Insecure client-server interoperability as a unit test."""

# NOTE(lidiz) This module only exists in Bazel BUILD file, for more details
# please refer to comments in the "bazel_namespace_package_hack" module.
try:
    from tests import bazel_namespace_package_hack

    bazel_namespace_package_hack.sys_path_to_site_dir_hack()
except ImportError:
    pass

import unittest

import grpc

from src.proto.grpc.testing import test_pb2_grpc
from tests.interop import _intraop_test_case
from tests.interop import otel_interop_helper
from tests.interop import server
from tests.interop import service
from tests.unit import test_common


class InsecureIntraopTest(
    _intraop_test_case.IntraopTestCase, unittest.TestCase
):
    def setUp(self):
        self.server = test_common.test_server()
        test_pb2_grpc.add_TestServiceServicer_to_server(
            service.TestService(), self.server
        )
        port = self.server.add_insecure_port("[::]:0")
        self.server.start()
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.insecure_channel("localhost:{}".format(port))
        )

    def tearDown(self):
        self.server.stop(None)


class OTelInteropHelperTest(unittest.TestCase):
    def test_pack_and_unpack_grpc_trace_bin_roundtrip(self):
        trace_id = 0x4BF92F3577B34DA6A3CE929D0E0E4736
        span_id = 0x00F067AA0BA902B7
        packed = otel_interop_helper.pack_grpc_trace_bin(
            trace_id, span_id, is_sampled=True
        )
        self.assertEqual(len(packed), 29)
        self.assertEqual(packed[0], 0)
        self.assertEqual(packed[1], 0)
        self.assertEqual(packed[18], 1)
        self.assertEqual(packed[27], 2)
        self.assertEqual(packed[28], 1)

        unpacked_trace_id, unpacked_span_id, is_sampled = (
            otel_interop_helper.unpack_grpc_trace_bin(packed)
        )
        self.assertEqual(unpacked_trace_id, trace_id)
        self.assertEqual(unpacked_span_id, span_id)
        self.assertTrue(is_sampled)

    def test_pack_grpc_trace_bin_hex_string_and_bytes(self):
        trace_hex = "4bf92f3577b34da6a3ce929d0e0e4736"
        span_hex = "00f067aa0ba902b7"
        packed_from_hex = otel_interop_helper.pack_grpc_trace_bin(
            trace_hex, span_hex, is_sampled=False
        )
        self.assertEqual(len(packed_from_hex), 29)
        unpacked_trace_id, unpacked_span_id, is_sampled = (
            otel_interop_helper.unpack_grpc_trace_bin(packed_from_hex)
        )
        self.assertEqual(unpacked_trace_id, int(trace_hex, 16))
        self.assertEqual(unpacked_span_id, int(span_hex, 16))
        self.assertFalse(is_sampled)

        trace_bytes = bytes.fromhex(trace_hex)
        span_bytes = bytes.fromhex(span_hex)
        packed_from_bytes = otel_interop_helper.pack_grpc_trace_bin(
            trace_bytes, span_bytes, is_sampled=True
        )
        self.assertEqual(packed_from_bytes[28], 1)
        unpacked_trace_id2, unpacked_span_id2, is_sampled2 = (
            otel_interop_helper.unpack_grpc_trace_bin(packed_from_bytes)
        )
        self.assertEqual(unpacked_trace_id2, int(trace_hex, 16))
        self.assertEqual(unpacked_span_id2, int(span_hex, 16))
        self.assertTrue(is_sampled2)

    def test_format_and_parse_traceparent_valid(self):
        trace_id = 0x4BF92F3577B34DA6A3CE929D0E0E4736
        span_id = 0x00F067AA0BA902B7
        formatted = otel_interop_helper.format_traceparent(
            trace_id, span_id, is_sampled=True
        )
        self.assertEqual(
            formatted,
            "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
        )
        self.assertEqual(len(formatted), 55)

        parsed_trace_id, parsed_span_id, is_sampled = (
            otel_interop_helper.parse_traceparent(formatted)
        )
        self.assertEqual(parsed_trace_id, trace_id)
        self.assertEqual(parsed_span_id, span_id)
        self.assertTrue(is_sampled)

    def test_parse_traceparent_invalid(self):
        self.assertEqual(
            otel_interop_helper.parse_traceparent("00-1234-5678-01"),
            (None, None, False),
        )
        self.assertEqual(
            otel_interop_helper.parse_traceparent(
                "01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"
            ),
            (None, None, False),
        )
        self.assertEqual(
            otel_interop_helper.parse_traceparent(
                "00-00000000000000000000000000000000-00f067aa0ba902b7-01"
            ),
            (None, None, False),
        )
        self.assertEqual(
            otel_interop_helper.parse_traceparent(
                "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01"
            ),
            (None, None, False),
        )
        self.assertEqual(
            otel_interop_helper.parse_traceparent(
                "00-4bf92f3577b34da6a3ce929d0e0e473g-00f067aa0ba902b7-01"
            ),
            (None, None, False),
        )
        self.assertEqual(
            otel_interop_helper.parse_traceparent(
                "00-+bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"
            ),
            (None, None, False),
        )
        self.assertEqual(
            otel_interop_helper.parse_traceparent(12345),
            (None, None, False),
        )

    def test_unpack_grpc_trace_bin_base64_and_invalid(self):
        import base64
        trace_id = 0x4BF92F3577B34DA6A3CE929D0E0E4736
        span_id = 0x00F067AA0BA902B7
        packed = otel_interop_helper.pack_grpc_trace_bin(
            trace_id, span_id, is_sampled=True
        )
        b64_str = base64.b64encode(packed).decode("ascii")
        unpacked_t, unpacked_s, is_sampled = (
            otel_interop_helper.unpack_grpc_trace_bin(b64_str)
        )
        self.assertEqual(unpacked_t, trace_id)
        self.assertEqual(unpacked_s, span_id)
        self.assertTrue(is_sampled)

        # Invalid or truncated binary
        self.assertEqual(
            otel_interop_helper.unpack_grpc_trace_bin(b"short"),
            (None, None, False),
        )
        self.assertEqual(
            otel_interop_helper.unpack_grpc_trace_bin(12345),
            (None, None, False),
        )
        # Invalid version
        bad_version = b"\x01" + packed[1:]
        self.assertEqual(
            otel_interop_helper.unpack_grpc_trace_bin(bad_version),
            (None, None, False),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
