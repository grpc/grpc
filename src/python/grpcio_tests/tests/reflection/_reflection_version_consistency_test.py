import inspect
import re
import unittest
from pathlib import Path
from packaging import version


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
            return match.group(1)
    except FileNotFoundError:
        print(f"Warning: Could not find requirements.txt at {requirements_path}")
        return None
    return None

def get_generated_protobuf_version(reflection_module):
    """Inspects the source of the generated pb2 file to find its version"""
    try:
        source_code = inspect.getsource(reflection_module)
        match = re.search(r"# Protobuf Python Version: ([\d.]+)", source_code)
        if match:
            return match.group(1)
    except FileNotFoundError:
        return None
    return None


class ProtobufVersionTest(unittest.TestCase):
    def test_protobuf_version_consistency(self):
        try:
            import grpc_reflection.v1alpha.reflection_pb2 as reflection_pb2
        except ImportError:
            self.fail(
                "Fatal: Could not import grpc_reflection.v1alpha.reflection_pb2. "
            )

        minimum_protobuf_version_str = get_minimum_protobuf_version()
        generated_protobuf_version_str = get_generated_protobuf_version(reflection_pb2)
        
        self.assertIsNotNone(
            minimum_protobuf_version_str,
            "Could not parse minimum Protobuf version from requirements.txt"
        )
        self.assertIsNotNone(
            generated_protobuf_version_str,
            "Could not parse Protobuf version from generated reflection_pb2.py file"
        )
        
        minimum_protobuf_version = version.parse(minimum_protobuf_version_str)
        generated_protobuf_version = version.parse(generated_protobuf_version_str)

        failure_message = (
            f"Detected incompatible Protobuf Gencode/Minimum Required Runtime version "
            f"grpc_reflection/v1alpha/reflection.proto: gencode ({generated_protobuf_version}) "
            f"minimum required runtime version ({minimum_protobuf_version}). "
            f"Minimum Required runtime versions cannot be older than the linked gencode version. "
        )
        
        self.assertGreaterEqual(
            minimum_protobuf_version,
            generated_protobuf_version,
            failure_message,
        )
        
if __name__ == "__main__":
    unittest.main()