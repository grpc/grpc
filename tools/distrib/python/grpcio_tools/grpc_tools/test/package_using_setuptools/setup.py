# Copyright 2022 gRPC authors.
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

import setuptools


setuptools.setup(
    name='test-package-setuptools-hook',
    version='0.0.1',
    description="A sample package to uses setuptools entrypoint functionality to generate files",
    url='https://grpc.io',
    author='The gRPC Authors',
    author_email='grpc-io@googlegroups.com',
    packages=["my_module"],
    python_requires=">=3.6",
    setup_requires=["grpcio_tools"],
)
