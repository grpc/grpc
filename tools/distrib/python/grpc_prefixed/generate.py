# Copyright 2020 The gRPC Authors
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
"""Generates grpc-prefixed packages using template renderer.

To use this script, please use 3.7+ interpreter. This script is work-directory
agnostic. A quick executable command:

    python3 tools/distrib/python/grpc_prefixed/generate.py
"""

import dataclasses
import datetime
import logging
import os
import shutil
import subprocess
import sys

import jinja2

WORK_PATH = os.path.realpath(os.path.dirname(__file__))
LICENSE = os.path.join(WORK_PATH, '../../../../LICENSE')
BUILD_PATH = os.path.join(WORK_PATH, 'build')
DIST_PATH = os.path.join(WORK_PATH, 'dist')

env = jinja2.Environment(
    loader=jinja2.FileSystemLoader(os.path.join(WORK_PATH, 'templates')))

LOGGER = logging.getLogger(__name__)
POPEN_TIMEOUT_S = datetime.timedelta(minutes=1).total_seconds()


@dataclasses.dataclass
class PackageMeta:
    """Meta-info of a PyPI package."""
    name: str
    name_long: str
    destination_package: str
    version: str = '1.0.0'


def clean() -> None:
    try:
        shutil.rmtree(BUILD_PATH)
    except FileNotFoundError:
        pass

    try:
        shutil.rmtree(DIST_PATH)
    except FileNotFoundError:
        pass


def generate_package(meta: PackageMeta) -> None:
    # Makes package directory
    package_path = os.path.join(BUILD_PATH, meta.name)
    os.makedirs(package_path, exist_ok=True)

    # Copy license
    shutil.copyfile(LICENSE, os.path.join(package_path, 'LICENSE'))

    # Generates source code
    for template_name in env.list_templates():
        template = env.get_template(template_name)
        with open(
                os.path.join(package_path,
                             template_name.replace('.template', '')), 'w') as f:
            f.write(template.render(dataclasses.asdict(meta)))

    # Creates wheel
    job = subprocess.Popen([
        sys.executable,
        os.path.join(package_path, 'setup.py'), 'sdist', '--dist-dir', DIST_PATH
    ],
                           cwd=package_path,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
    outs, _ = job.communicate(timeout=POPEN_TIMEOUT_S)

    # Logs result
    if job.returncode != 0:
        LOGGER.error('Wheel creation failed with %d', job.returncode)
        LOGGER.error(outs)
    else:
        LOGGER.info('Package <%s> generated', meta.name)


def main():
    clean()

    generate_package(
        PackageMeta(name='grpc',
                    name_long='gRPC Python',
                    destination_package='grpcio'))

    generate_package(
        PackageMeta(name='grpc-status',
                    name_long='gRPC Rich Error Status',
                    destination_package='grpcio-status'))

    generate_package(
        PackageMeta(name='grpc-channelz',
                    name_long='gRPC Channel Tracing',
                    destination_package='grpcio-channelz'))

    generate_package(
        PackageMeta(name='grpc-tools',
                    name_long='ProtoBuf Code Generator',
                    destination_package='grpcio-tools'))

    generate_package(
        PackageMeta(name='grpc-reflection',
                    name_long='gRPC Reflection',
                    destination_package='grpcio-reflection'))

    generate_package(
        PackageMeta(name='grpc-testing',
                    name_long='gRPC Testing Utility',
                    destination_package='grpcio-testing'))

    generate_package(
        PackageMeta(name='grpc-health-checking',
                    name_long='gRPC Health Checking',
                    destination_package='grpcio-health-checking'))

    generate_package(
        PackageMeta(name='grpc-csds',
                    name_long='gRPC Client Status Discovery Service',
                    destination_package='grpcio-csds'))

    generate_package(
        PackageMeta(name='grpc-admin',
                    name_long='gRPC Admin Interface',
                    destination_package='grpcio-admin'))


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    main()
