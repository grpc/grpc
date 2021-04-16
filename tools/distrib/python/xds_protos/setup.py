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
"""A PyPI package for xDS protos generated Python code."""

import sys
import os
import setuptools
import pkg_resources

from grpc_tools import protoc

# We might not want to compile all the protos
EXCLUDE_PROTO_PACKAGES_LIST = [
    # Requires extra dependency to Prometheus protos
    'envoy/service/metrics/v2',
    'envoy/service/metrics/v3',
    'envoy/service/metrics/v4alpha',
]

# Compute the pathes
WORK_DIR = os.path.dirname(os.path.abspath(__file__))
GRPC_ROOT = os.path.abspath(os.path.join(WORK_DIR, '..', '..', '..', '..'))
XDS_PROTO_ROOT = os.path.join(GRPC_ROOT, 'third_party', 'envoy-api')
UDPA_PROTO_ROOT = os.path.join(GRPC_ROOT, 'third_party', 'udpa')
GOOGLEAPIS_ROOT = os.path.join(GRPC_ROOT, 'third_party', 'googleapis')
VALIDATE_ROOT = os.path.join(GRPC_ROOT, 'third_party', 'protoc-gen-validate')
OPENCENSUS_PROTO_ROOT = os.path.join(GRPC_ROOT, 'third_party',
                                     'opencensus-proto', 'src')
WELL_KNOWN_PROTOS_INCLUDE = pkg_resources.resource_filename(
    'grpc_tools', '_proto')
OUTPUT_PATH = WORK_DIR

# Prepare the test file generation
TEST_FILE_NAME = 'generated_file_import_test.py'
TEST_IMPORTS = []


def add_test_import(proto_package_path: str,
                    file_name: str,
                    service: bool = False):
    TEST_IMPORTS.append("from %s import %s\n" % (proto_package_path.replace(
        '/', '.'), file_name.replace('.proto', '_pb2')))
    if service:
        TEST_IMPORTS.append("from %s import %s\n" % (proto_package_path.replace(
            '/', '.'), file_name.replace('.proto', '_pb2_grpc')))


# Prepare Protoc command
COMPILE_PROTO_ONLY = [
    'grpc_tools.protoc',
    '--proto_path={}'.format(XDS_PROTO_ROOT),
    '--proto_path={}'.format(UDPA_PROTO_ROOT),
    '--proto_path={}'.format(GOOGLEAPIS_ROOT),
    '--proto_path={}'.format(VALIDATE_ROOT),
    '--proto_path={}'.format(WELL_KNOWN_PROTOS_INCLUDE),
    '--proto_path={}'.format(OPENCENSUS_PROTO_ROOT),
    '--python_out={}'.format(OUTPUT_PATH),
]
COMPILE_BOTH = COMPILE_PROTO_ONLY + ['--grpc_python_out={}'.format(OUTPUT_PATH)]


# Compile xDS protos
def has_grpc_service(proto_package_path: str) -> bool:
    return proto_package_path.startswith('envoy/service')


def compile_protos(proto_root: str, sub_dir: str = '.') -> None:
    for root, _, files in os.walk(os.path.join(proto_root, sub_dir)):
        proto_package_path = os.path.relpath(root, proto_root)
        if proto_package_path in EXCLUDE_PROTO_PACKAGES_LIST:
            print(f'Skipping package {proto_package_path}')
            continue
        for file_name in files:
            if file_name.endswith('.proto'):
                # Compile proto
                if has_grpc_service(proto_package_path):
                    return_code = protoc.main(COMPILE_BOTH +
                                              [os.path.join(root, file_name)])
                    add_test_import(proto_package_path, file_name, service=True)
                else:
                    return_code = protoc.main(COMPILE_PROTO_ONLY +
                                              [os.path.join(root, file_name)])
                    add_test_import(proto_package_path,
                                    file_name,
                                    service=False)
                if return_code != 0:
                    raise Exception('error: {} failed'.format(COMPILE_BOTH))


compile_protos(XDS_PROTO_ROOT)
compile_protos(UDPA_PROTO_ROOT)
# We don't want to compile the entire GCP surface API, just the essential ones
compile_protos(GOOGLEAPIS_ROOT, os.path.join('google', 'api'))
compile_protos(GOOGLEAPIS_ROOT, os.path.join('google', 'rpc'))
compile_protos(GOOGLEAPIS_ROOT, os.path.join('google', 'longrunning'))
compile_protos(GOOGLEAPIS_ROOT, os.path.join('google', 'logging'))
compile_protos(GOOGLEAPIS_ROOT, os.path.join('google', 'type'))
compile_protos(VALIDATE_ROOT, 'validate')
compile_protos(OPENCENSUS_PROTO_ROOT)


# Generate __init__.py files for
def create_init_file(path: str) -> None:
    f = open(os.path.join(path, "__init__.py"), 'w')
    f.close()


create_init_file(WORK_DIR)
for root, _, _ in os.walk(os.path.join(WORK_DIR, 'envoy')):
    create_init_file(root)

# Generate test file
with open(os.path.join(WORK_DIR, TEST_FILE_NAME), 'w') as f:
    f.writelines(TEST_IMPORTS)

# Use setuptools to build Python package
CLASSIFIERS = [
    'Development Status :: 3 - Alpha',
    'Programming Language :: Python',
    'Programming Language :: Python :: 2',
    'Programming Language :: Python :: 3',
    'License :: OSI Approved :: Apache Software License',
]
INSTALL_REQUIRES = [
    'protobuf',
    'grpcio',
]
SETUP_REQUIRES = INSTALL_REQUIRES + ["grpcio-tools"]
setuptools.setup(
    name='xds-protos',
    version='0.0.1',
    packages=setuptools.find_packages(where=".", exclude=[TEST_FILE_NAME]),
    description='Generated Python code from envoyproxy/data-plane-api',
    author='The gRPC Authors',
    author_email='grpc-io@googlegroups.com',
    url='https://grpc.io',
    license='Apache License 2.0',
    install_requires=INSTALL_REQUIRES,
    setup_requires=SETUP_REQUIRES,
    classifiers=CLASSIFIERS)
