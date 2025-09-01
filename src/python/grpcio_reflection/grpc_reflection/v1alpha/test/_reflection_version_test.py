import unittest
import re
import importlib.util
from pathlib import Path


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


def find_repo_requirements(start_path: Path) -> Path:
    """Walk up from start_path to filesystem root and return first requirements.txt found."""
    for p in [start_path] + list(start_path.parents):
        candidate = p / "requirements.txt"
        if candidate.exists():
            return candidate
    return None

def parse_protobuf_min_from_requirements(req_text: str):
    """
    Parse protobuf min version from a requirements.txt-like text.
    Returns version string (e.g. '5.26.1') or None.
    """
    for line in req_text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "protobuf" not in line.lower():
            continue
        m = re.search(r"protobuf\s*[>=~!]*\s*([0-9]+\.[0-9]+(?:\.[0-9]+)?)", line, re.IGNORECASE)
        if m:
            return m.group(1)
    return None

def parse_gencode_min_protobuf_from_pb2(reflection_pb2_content: str):
    """
    Parse gencode protobuf min version from reflection_pb2.py content.
    Extracts the version tuple from a line like:
        ValidateProtobufRuntimeVersion(proto_module, major, minor, patch)
    Args:
        reflection_pb2_content (str): The contents of reflection_pb2.py as a string.
    Returns:
        tuple[int, int, int] | None: Version tuple (e.g., (5, 26, 1)) if found,
        otherwise None.
    """
    match = re.search(
        r"ValidateProtobufRuntimeVersion\([^,]*,\s*(\d+),\s*(\d+),\s*(\d+)",
        reflection_pb2_content,
    )
    gencode_protobuf_version_tuple = None
    if match:
        return tuple(map(int, match.groups()))
    else:
        return None

class TestProtobufReflectionConsistency(unittest.TestCase):
    def test_reflection_pb2_vs_requirements(self):
        # 1) Locate reflection_pb2 source 
        module_name = "grpc_reflection.v1alpha.reflection_pb2"
        module_spec = importlib.util.find_spec(module_name)
        if module_spec is None:
            self.fail(f"Module spec not found for {module_name}")
        reflection_pb2_path = Path(module_spec.origin)

        if not reflection_pb2_path.exists():
            self.fail(f"reflection_pb2.py file not found at {reflection_pb2_path}")

        #Read pb2 file content
        reflection_pb2_content = reflection_pb2_path.read_text(encoding="utf-8")
        # Extract gencode version from ValidateProtobufRuntimeVersion(...) 
        gencode_protobuf_version_tuple = parse_gencode_min_protobuf_from_pb2(reflection_pb2_content)
        self.assertIsNotNone(gencode_protobuf_version_tuple, "Could not determine gencode version from reflection_pb2.py")
        
        #Find requirements.txt in repo root
        requirment_txt_file_dir = Path(__file__).resolve().parent
        requirment_txt_file = find_repo_requirements(requirment_txt_file_dir)
        self.assertIsNotNone(requirment_txt_file, "Could not find repository requirements.txt")

        #Read requirment.txt file content
        req_text_file_content = requirment_txt_file.read_text(encoding="utf-8")
        minimum_protobuf_version = parse_protobuf_min_from_requirements(req_text_file_content)
        self.assertIsNotNone(minimum_protobuf_version, "No minimum protobuf version found in requirements.txt")

        #Convert version string like to tuple
        minimum_protobuf_version_tuple = version_tuple_from_str(minimum_protobuf_version)

        #Assert minimum_protobuf_version_tuple >= gencode_protobuf_version_tuple
        self.assertGreaterEqual(
            minimum_protobuf_version_tuple,
            gencode_protobuf_version_tuple,
            msg=(
                f"requirements.txt declares protobuf>={minimum_protobuf_version_tuple} "
                f"but generated code requires >= {gencode_protobuf_version_tuple}. "
            ),
        )

if __name__ == "__main__":
    unittest.main()
