# Copyright 2025 The gRPC Authors
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
"""A setup module for gRPC Sleuth Python package."""

import os
import shutil
import subprocess
import sys

import python_version
import setuptools
from setuptools import Extension
from setuptools.command.build_ext import build_ext

import grpc_version

_PACKAGE_PATH = os.path.realpath(os.path.dirname(__file__))
_README_PATH = os.path.join(_PACKAGE_PATH, "README.rst")
GRPC_ROOT = os.path.abspath(os.path.join(_PACKAGE_PATH, "../../.."))
# Path to the directory containing sleuth.so
_SLEUTH_SO_SRC = os.path.join(GRPC_ROOT, "bazel-bin/test/cpp/sleuth/sleuth.so")

# Relative directory paths from GRPC_ROOT
SLEUTH_HDR_PATTERNS = [
    "test/cpp/sleuth",
    "third_party/abseil-cpp",
]
sleuth_include_dirs = set()
for pattern in SLEUTH_HDR_PATTERNS:
    sleuth_include_dirs.add(os.path.join(GRPC_ROOT, pattern))


class custom_build_ext(build_ext):
    def run(self):
        # Run the bazel build command
        bazel_cmd = ["bazel", "build", "//test/cpp/sleuth:sleuth.so"]
        print(f"Running command: {' '.join(bazel_cmd)}")
        result = subprocess.run(bazel_cmd, cwd=GRPC_ROOT, check=False)
        if result.returncode != 0:
            print("Bazel build failed!")
            sys.exit(result.returncode)
        print("Bazel build successful.")

        # --- Copy .so into the source tree for package_data ---
        # This ensures the .so file is included in the wheel.
        source_lib_dir = os.path.join(_PACKAGE_PATH, "grpc_sleuth", "lib")
        os.makedirs(source_lib_dir, exist_ok=True)
        sleuth_so_dest_src = os.path.join(source_lib_dir, "libsleuth.so")
        if os.path.exists(sleuth_so_dest_src):
            os.remove(sleuth_so_dest_src)
        shutil.copy(_SLEUTH_SO_SRC, sleuth_so_dest_src)
        print(f"Copied {_SLEUTH_SO_SRC} to {sleuth_so_dest_src} for packaging")

        # --- Copy .so into the build directory for linking the extension ---
        # This makes the .so available during the extension build process.
        build_lib_dest_dir = os.path.join(self.build_lib, "grpc_sleuth", "lib")
        os.makedirs(build_lib_dest_dir, exist_ok=True)
        shutil.copy(_SLEUTH_SO_SRC, os.path.join(build_lib_dest_dir, "libsleuth.so"))

        # Add the library directory for the current build to find libsleuth.so during extension linking
        for ext in self.extensions:
            ext.library_dirs.append(build_lib_dest_dir)

        super().run()


CLASSIFIERS = [
    "Development Status :: 5 - Production/Stable",
    "Programming Language :: Python",
    "Programming Language :: Python :: 3",
    "License :: OSI Approved :: Apache Software License",
]

PACKAGE_DIRECTORIES = {
    "": ".",
}

INSTALL_REQUIRES = [
    "protobuf>=6.31.1,<7.0.0",
    "absl-py>=1.4.0",
]
SETUP_REQUIRES = ["Cython>=0.29.21"]

extensions = [
    Extension(
        "grpc_sleuth.sleuth_lib",
        sources=["grpc_sleuth/sleuth_lib.pyx"],
        include_dirs=sorted(list(sleuth_include_dirs)),
        define_macros=[
            ("Py_LIMITED_API", 0x030A0000),
        ],
        py_limited_api=True,
        language="c++",
        extra_compile_args=["-std=c++17"],
        libraries=["sleuth"],
        runtime_library_dirs=["$ORIGIN/lib"]
    )
]

setuptools.setup(
    name="grpcio-sleuth",
    version=grpc_version.VERSION,
    description="gRPC Sleuth tool",
    long_description=open(_README_PATH, "r").read(),
    author="The gRPC Authors",
    author_email="grpc-io@googlegroups.com",
    url="https://grpc.io",
    license="Apache License 2.0",
    classifiers=CLASSIFIERS,
    package_dir=PACKAGE_DIRECTORIES,
    packages=setuptools.find_packages("."),
    python_requires=f">={python_version.MIN_PYTHON_VERSION}",
    install_requires=INSTALL_REQUIRES,
    setup_requires=SETUP_REQUIRES,
    ext_modules=extensions,
    cmdclass={
        'build_ext': custom_build_ext,
    },
    entry_points={
        'console_scripts': [
            'grpc_sleuth=grpc_sleuth.sleuth_cli:main',
        ],
    },
    package_data={'grpc_sleuth': ['lib/*.so']},
    include_package_data=True,
)
