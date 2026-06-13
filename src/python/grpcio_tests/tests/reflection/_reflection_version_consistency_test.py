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

import inspect
from pathlib import Path
import re
import unittest


def version_tuple_from_str(version_str):
    """Convert version string like '5.27.5' or '5.27.5.post1' -> (5,27,5)."""
    if version_str is None:
        return None
    match = re.match(r"^\s*(\d+)(?:\.(\d+))?(?:\.(\d+))?", version_str)
    if not match:
        return None
    parts = [int(x) if x is not None else 0 for x in match.groups()]
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts[:3])


def get_minimum_protobuf_version():
    """Parses requirements.txt to find the minimum protobuf version"""
    try:
        script_dir = Path(__file__).resolve().parent
        project_root = script_dir.parents[4]
        requirements_path = project_root / "requirements.txt"

        with open(requirements_path, "r") as f:
            content = f.read()

        match = re.search(r"protobuf\s*>=?\s*([\d.]+)", content, re.IGNORECASE)
        if match:
            return version_tuple_from_str(match.group(1))
    except FileNotFoundError:
        print(
            f"Warning: Could not find requirements.txt at {requirements_path}"
        )
        return None
    return None


def get_generated_protobuf_version(reflection_module):
    """Inspects the source of the generated pb2 file to find its version"""
    try:
        source_code = inspect.getsource(reflection_module)
        match = re.search(r"# Protobuf Python Version: ([\d.]+)", source_code)
        if match:
            return version_tuple_from_str(match.group(1))
    except FileNotFoundError:
        return None
    return None


class ProtobufVersionTest(unittest.TestCase):
    def test_protobuf_version_consistency(self):
        try:
            from grpc_reflection.v1alpha import reflection_pb2
        except ImportError:
            self.fail(
                "Fatal: Could not import grpc_reflection.v1alpha.reflection_pb2. "
            )

        minimum_version_tuple = get_minimum_protobuf_version()
        generated_version_tuple = get_generated_protobuf_version(reflection_pb2)

        self.assertIsNotNone(
            minimum_version_tuple,
            "Could not parse minimum Protobuf version from requirements.txt",
        )
        self.assertIsNotNone(
            generated_version_tuple,
            "Could not parse Protobuf version from generated reflection_pb2.py file",
        )

        failure_message = (
            f"Detected incompatible Protobuf Gencode/Minimum Required Runtime version "
            f"grpc_reflection/v1alpha/reflection.proto: gencode ({generated_version_tuple}) "
            f"minimum required runtime version ({minimum_version_tuple}). "
            f"Minimum Required runtime versions cannot be older than the linked gencode version. "
        )

        self.assertGreaterEqual(
            minimum_version_tuple,
            generated_version_tuple,
            failure_message,
        )


if __name__ == "__main__":
    unittest.main()
