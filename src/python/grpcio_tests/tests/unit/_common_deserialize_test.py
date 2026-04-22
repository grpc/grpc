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

"""Tests for grpc._common.deserialize with memoryview and list inputs."""

import unittest

from grpc import _common


class DeserializeTest(unittest.TestCase):
    """Tests the deserialize function handles the new input types
    introduced by the memoryview optimization in ReceiveMessageOperation."""

    def test_deserialize_bytes(self):
        """Standard bytes input should pass through unchanged."""
        data = b"hello world"
        result = _common.deserialize(data, None)
        self.assertEqual(result, data)
        self.assertIsInstance(result, bytes)

    def test_deserialize_memoryview(self):
        """memoryview input (single-slice path) should be converted to bytes."""
        data = b"hello world"
        mv = memoryview(data)
        result = _common.deserialize(mv, None)
        self.assertEqual(result, data)
        self.assertIsInstance(result, bytes)

    def test_deserialize_list_of_bytes(self):
        """list input (multi-slice path) should be joined into bytes."""
        chunks = [b"hello", b" ", b"world"]
        result = _common.deserialize(chunks, None)
        self.assertEqual(result, b"hello world")
        self.assertIsInstance(result, bytes)

    def test_deserialize_list_single_chunk(self):
        """list with a single chunk should still be joined correctly."""
        chunks = [b"hello world"]
        result = _common.deserialize(chunks, None)
        self.assertEqual(result, b"hello world")
        self.assertIsInstance(result, bytes)

    def test_deserialize_empty_list(self):
        """Empty list should produce empty bytes."""
        result = _common.deserialize([], None)
        self.assertEqual(result, b"")
        self.assertIsInstance(result, bytes)

    def test_deserialize_empty_memoryview(self):
        """Empty memoryview should produce empty bytes."""
        result = _common.deserialize(memoryview(b""), None)
        self.assertEqual(result, b"")
        self.assertIsInstance(result, bytes)

    def test_deserialize_list_of_memoryviews(self):
        """list of memoryview objects (actual multi-slice output) should join."""
        chunks = [memoryview(b"hello"), memoryview(b" world")]
        result = _common.deserialize(chunks, None)
        self.assertEqual(result, b"hello world")
        self.assertIsInstance(result, bytes)

    def test_deserialize_with_deserializer_bytes(self):
        """Deserializer should receive bytes from standard input."""
        received = []

        def capture(data):
            received.append(data)
            return data

        data = b"test"
        _common.deserialize(data, capture)
        self.assertEqual(received, [b"test"])

    def test_deserialize_with_deserializer_memoryview(self):
        """Deserializer should receive bytes converted from memoryview."""
        received = []

        def capture(data):
            received.append(data)
            return data

        _common.deserialize(memoryview(b"test"), capture)
        self.assertEqual(len(received), 1)
        self.assertEqual(received[0], b"test")
        self.assertIsInstance(received[0], bytes)

    def test_deserialize_with_deserializer_list(self):
        """Deserializer should receive joined bytes from list input."""
        received = []

        def capture(data):
            received.append(data)
            return data

        _common.deserialize([b"a", b"b", b"c"], capture)
        self.assertEqual(len(received), 1)
        self.assertEqual(received[0], b"abc")
        self.assertIsInstance(received[0], bytes)

    def test_deserialize_large_memoryview(self):
        """Large memoryview should be handled correctly."""
        data = b"x" * (1024 * 1024)  # 1MB
        result = _common.deserialize(memoryview(data), None)
        self.assertEqual(result, data)

    def test_deserialize_large_list(self):
        """Large list of chunks should be handled correctly."""
        chunk = b"x" * 1024
        chunks = [chunk] * 1024  # 1MB total
        result = _common.deserialize(chunks, None)
        self.assertEqual(len(result), 1024 * 1024)


if __name__ == "__main__":
    unittest.main(verbosity=2)
