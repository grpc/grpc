import os

from Cython.Build import cythonize
from setuptools import Extension
from setuptools import setup

# Attempt to find absl relative to this directory
curr_dir = os.path.abspath(os.path.dirname(__file__))

# Find gRPC root by looking for third_party
grpc_root = curr_dir
while grpc_root != os.path.dirname(grpc_root):
    if os.path.exists(os.path.join(grpc_root, "third_party", "abseil-cpp")):
        break
    grpc_root = os.path.dirname(grpc_root)

absl_include = os.path.join(grpc_root, "third_party", "abseil-cpp")
absl_lib_dir = os.path.join(grpc_root, "cmake/build/third_party/abseil-cpp")

print(f"DEBUG: curr_dir: {curr_dir}")
print(f"DEBUG: grpc_root: {grpc_root}")
print(f"DEBUG: absl_include: {absl_include}")
print(f"DEBUG: absl_lib_dir: {absl_lib_dir}")

include_dirs = ["."]
extra_objects = []

if os.path.exists(absl_include):
    print(f"DEBUG: Found absl include at {absl_include}")
    include_dirs.append(absl_include)
else:
    print(f"DEBUG: Abseil include NOT FOUND at {absl_include}")

if os.path.exists(absl_lib_dir):
    print(f"DEBUG: Searching for absl libraries in {absl_lib_dir}")
    for root, dirs, files in os.walk(absl_lib_dir):
        for file in files:
            if file.endswith(".a"):
                extra_objects.append(os.path.join(root, file))
    print(f"DEBUG: Found {len(extra_objects)} absl static libraries")
else:
    print(f"DEBUG: Abseil library directory NOT FOUND at {absl_lib_dir}")

# Allow manual override via environment variable
if os.environ.get("ABSL_INCLUDE"):
    include_dirs.append(os.environ.get("ABSL_INCLUDE"))


extensions = [
    Extension(
        "hello_cython",
        sources=[
            "hello_cython.pyx",
            "hello.cc",
            "third_party/abseil-cpp/absl/log/log.cc",
            "third_party/abseil-cpp/absl/log/initialize.cc",
        ],
        include_dirs=include_dirs,
        extra_link_args=["-Wl,--whole-archive"]
        + extra_objects
        + ["-Wl,--no-whole-archive"],
        language="c++",
        extra_compile_args=["-std=c++17"],
    )
]

setup(
    name="hello_app",
    ext_modules=cythonize(extensions),
)
