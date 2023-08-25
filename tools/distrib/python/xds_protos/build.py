#! /usr/bin/env python3
# Copyright 2021 The gRPC Authors
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
"""Builds the content of xds-protos package"""

import os

from grpc_tools import protoc
import pkg_resources


def localize_path(p):
    return os.path.join(*p.split("/"))


# We might not want to compile all the protos
EXCLUDE_PROTO_PACKAGES_LIST = tuple(
    localize_path(p)
    for p in (
        # Requires extra dependency to Prometheus protos
        "envoy/service/metrics/v2",
        "envoy/service/metrics/v3",
        "envoy/service/metrics/v4alpha",
    )
)

# Compute the pathes
WORK_DIR = os.path.dirname(os.path.abspath(__file__))
GRPC_ROOT = os.path.abspath(os.path.join(WORK_DIR, "..", "..", "..", ".."))
ENVOY_API_PROTO_ROOT = os.path.join(GRPC_ROOT, "third_party", "envoy-api")
XDS_PROTO_ROOT = os.path.join(GRPC_ROOT, "third_party", "xds")
GOOGLEAPIS_ROOT = os.path.join(GRPC_ROOT, "third_party", "googleapis")
VALIDATE_ROOT = os.path.join(GRPC_ROOT, "third_party", "protoc-gen-validate")
OPENCENSUS_PROTO_ROOT = os.path.join(
    GRPC_ROOT, "third_party", "opencensus-proto", "src"
)
OPENTELEMETRY_PROTO_ROOT = os.path.join(
    GRPC_ROOT, "third_party", "opentelemetry"
)
WELL_KNOWN_PROTOS_INCLUDE = pkg_resources.resource_filename(
    "grpc_tools", "_proto"
)

OUTPUT_PATH = WORK_DIR

# Prepare the test file generation
INIT_FILE_NAME = "__init__.py"
TEST_IMPORTS = []

# The pkgutil-style namespace packaging __init__.py
PKGUTIL_STYLE_INIT = (
    "__path__ = __import__('pkgutil').extend_path(__path__, __name__)\n"
)
NAMESPACE_PACKAGES = ["google"]


def add_test_import(
    proto_package_path: str, file_name: str, service: bool = False
):
    TEST_IMPORTS.append(
        "from %s import %s\n"
        % (
            proto_package_path.replace("/", "."),
            file_name.replace(".proto", "_pb2"),
        )
    )
    if service:
        TEST_IMPORTS.append(
            "from %s import %s\n"
            % (
                proto_package_path.replace("/", "."),
                file_name.replace(".proto", "_pb2_grpc"),
            )
        )


# Prepare Protoc command
COMPILE_PROTO_ONLY = [
    "grpc_tools.protoc",
    "--proto_path={}".format(ENVOY_API_PROTO_ROOT),
    "--proto_path={}".format(XDS_PROTO_ROOT),
    "--proto_path={}".format(GOOGLEAPIS_ROOT),
    "--proto_path={}".format(VALIDATE_ROOT),
    "--proto_path={}".format(WELL_KNOWN_PROTOS_INCLUDE),
    "--proto_path={}".format(OPENCENSUS_PROTO_ROOT),
    "--proto_path={}".format(OPENTELEMETRY_PROTO_ROOT),
    "--python_out={}".format(OUTPUT_PATH),
]
COMPILE_BOTH = COMPILE_PROTO_ONLY + ["--grpc_python_out={}".format(OUTPUT_PATH)]


def has_grpc_service(proto_package_path: str) -> bool:
    return proto_package_path.startswith("envoy/service")


def compile_protos(proto_root: str, sub_dir: str = ".") -> None:
    for root, _, files in os.walk(os.path.join(proto_root, sub_dir)):
        proto_package_path = os.path.relpath(root, proto_root)
        if proto_package_path in EXCLUDE_PROTO_PACKAGES_LIST:
            print(f"Skipping package {proto_package_path}")
            continue
        for file_name in files:
            if file_name.endswith(".proto"):
                # Compile proto
                if has_grpc_service(proto_package_path):
                    return_code = protoc.main(
                        COMPILE_BOTH + [os.path.join(root, file_name)]
                    )
                    add_test_import(proto_package_path, file_name, service=True)
                else:
                    return_code = protoc.main(
                        COMPILE_PROTO_ONLY + [os.path.join(root, file_name)]
                    )
                    add_test_import(
                        proto_package_path, file_name, service=False
                    )
                if return_code != 0:
                    raise Exception("error: {} failed".format(COMPILE_BOTH))


def create_init_file(path: str, package_path: str = "") -> None:
    with open(os.path.join(path, "__init__.py"), "w") as f:
        # Apply the pkgutil-style namespace packaging, which is compatible for 2
        # and 3. Here is the full table of namespace compatibility:
        # https://github.com/pypa/sample-namespace-packages/blob/master/table.md
        if package_path in NAMESPACE_PACKAGES:
            f.write(PKGUTIL_STYLE_INIT)


def main():
    # Compile xDS protos
    compile_protos(ENVOY_API_PROTO_ROOT)
    compile_protos(XDS_PROTO_ROOT)
    # We don't want to compile the entire GCP surface API, just the essential ones
    compile_protos(GOOGLEAPIS_ROOT, os.path.join("google", "api"))
    compile_protos(GOOGLEAPIS_ROOT, os.path.join("google", "rpc"))
    compile_protos(GOOGLEAPIS_ROOT, os.path.join("google", "longrunning"))
    compile_protos(GOOGLEAPIS_ROOT, os.path.join("google", "logging"))
    compile_protos(GOOGLEAPIS_ROOT, os.path.join("google", "type"))
    compile_protos(VALIDATE_ROOT, "validate")
    compile_protos(OPENCENSUS_PROTO_ROOT)
    compile_protos(OPENTELEMETRY_PROTO_ROOT)

    # Generate __init__.py files for all modules
    create_init_file(WORK_DIR)
    for proto_root_module in [
        "envoy",
        "google",
        "opencensus",
        "udpa",
        "validate",
        "xds",
        "opentelemetry",
        "contrib",
    ]:
        for root, _, _ in os.walk(os.path.join(WORK_DIR, proto_root_module)):
            package_path = os.path.relpath(root, WORK_DIR)
            create_init_file(root, package_path)

    # Generate test file
    with open(os.path.join(WORK_DIR, INIT_FILE_NAME), "w") as f:
        f.writelines(TEST_IMPORTS)


if __name__ == "__main__":
    main()
