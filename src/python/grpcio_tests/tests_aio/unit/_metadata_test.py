# Copyright 2020 gRPC authors.
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
"""Tests for the metadata abstraction that's used in the asynchronous driver."""
import logging
import unittest

from grpc.experimental.aio import Metadata


class TestTypeMetadata(unittest.TestCase):
    """Tests for the metadata type"""

    _DEFAULT_DATA = (("key1", "value1"), ("key2", "value2"))
    _MULTI_ENTRY_DATA = (("key1", "value1"), ("key1", "other value 1"),
                         ("key2", "value2"))

    def test_init_metadata(self):
        test_cases = {
            "emtpy": (),
            "with-single-data": self._DEFAULT_DATA,
            "with-multi-data": self._MULTI_ENTRY_DATA,
        }
        for case, args in test_cases.items():
            with self.subTest(case=case):
                metadata = Metadata(*args)
                self.assertEqual(len(metadata), len(args))

    def test_get_item(self):
        metadata = Metadata(("key", "value1"), ("key", "value2"),
                            ("key2", "other value"))
        self.assertEqual(metadata["key"], "value1")
        self.assertEqual(metadata["key2"], "other value")
        self.assertEqual(metadata.get("key"), "value1")
        self.assertEqual(metadata.get("key2"), "other value")

        with self.assertRaises(KeyError):
            metadata["key not found"]
        self.assertIsNone(metadata.get("key not found"))

    def test_add_value(self):
        metadata = Metadata()
        metadata.add("key", "value")
        metadata.add("key", "second value")
        metadata.add("key2", "value2")

        self.assertEqual(metadata["key"], "value")
        self.assertEqual(metadata["key2"], "value2")

    def test_get_all_items(self):
        metadata = Metadata(*self._MULTI_ENTRY_DATA)
        self.assertEqual(metadata.get_all("key1"), ["value1", "other value 1"])
        self.assertEqual(metadata.get_all("key2"), ["value2"])
        self.assertEqual(metadata.get_all("non existing key"), [])

    def test_container(self):
        metadata = Metadata(*self._MULTI_ENTRY_DATA)
        self.assertIn("key1", metadata)

    def test_equals(self):
        metadata = Metadata()
        for key, value in self._DEFAULT_DATA:
            metadata.add(key, value)
        metadata2 = Metadata(*self._DEFAULT_DATA)

        self.assertEqual(metadata, metadata2)
        self.assertNotEqual(metadata, "foo")

    def test_repr(self):
        metadata = Metadata(*self._DEFAULT_DATA)
        expected = "Metadata({0!r})".format(self._DEFAULT_DATA)
        self.assertEqual(repr(metadata), expected)

    def test_set(self):
        metadata = Metadata(*self._MULTI_ENTRY_DATA)
        override_value = "override value"
        for _ in range(3):
            metadata["key1"] = override_value

        self.assertEqual(metadata["key1"], override_value)
        self.assertEqual(metadata.get_all("key1"),
                         [override_value, "other value 1"])

        empty_metadata = Metadata()
        for _ in range(3):
            empty_metadata["key"] = override_value

        self.assertEqual(empty_metadata["key"], override_value)
        self.assertEqual(empty_metadata.get_all("key"), [override_value])

    def test_set_all(self):
        metadata = Metadata(*self._DEFAULT_DATA)
        metadata.set_all("key", ["value1", b"new value 2"])

        self.assertEqual(metadata["key"], "value1")
        self.assertEqual(metadata.get_all("key"), ["value1", b"new value 2"])

    def test_delete_values(self):
        metadata = Metadata(*self._MULTI_ENTRY_DATA)
        del metadata["key1"]
        self.assertEqual(metadata.get("key1"), "other value 1")

        metadata.delete_all("key1")
        self.assertNotIn("key1", metadata)

        metadata.delete_all("key2")
        self.assertEqual(len(metadata), 0)

        with self.assertRaises(KeyError):
            del metadata["other key"]

    def test_metadata_from_tuple(self):
        scenarios = (
            (None, Metadata()),
            (Metadata(), Metadata()),
            (self._DEFAULT_DATA, Metadata(*self._DEFAULT_DATA)),
            (self._MULTI_ENTRY_DATA, Metadata(*self._MULTI_ENTRY_DATA)),
            (Metadata(*self._DEFAULT_DATA), Metadata(*self._DEFAULT_DATA)),
        )
        for source, expected in scenarios:
            with self.subTest(raw_metadata=source, expected=expected):
                self.assertEqual(expected, Metadata.from_tuple(source))


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
