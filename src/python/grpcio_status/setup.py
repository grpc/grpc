# Copyright 2018 The gRPC Authors
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
"""Setup module for the GRPC Python package's status mapping."""

import os

import setuptools

_PACKAGE_PATH = os.path.realpath(os.path.dirname(__file__))
_README_PATH = os.path.join(_PACKAGE_PATH, 'README.rst')

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Break import-style to ensure we can actually find our local modules.
import grpc_version


class _NoOpCommand(setuptools.Command):
    """No-op command."""

    description = ''
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        pass


CLASSIFIERS = [
    'Development Status :: 5 - Production/Stable',
    'Programming Language :: Python',
    'Programming Language :: Python :: 3',
    'Programming Language :: Python :: 3.4',
    'Programming Language :: Python :: 3.5',
    'Programming Language :: Python :: 3.6',
    'Programming Language :: Python :: 3.7',
    'Programming Language :: Python :: 3.8',
    'Programming Language :: Python :: 3.9',
    'Programming Language :: Python :: 3.10',
    'Programming Language :: Python :: 3.11',
    'License :: OSI Approved :: Apache Software License',
]

PACKAGE_DIRECTORIES = {
    '': '.',
}

INSTALL_REQUIRES = (
    'protobuf>=4.21.6',
    'grpcio>={version}'.format(version=grpc_version.VERSION),
    'googleapis-common-protos>=1.5.5',
)

try:
    import status_commands as _status_commands

    # we are in the build environment, otherwise the above import fails
    COMMAND_CLASS = {
        # Run preprocess from the repository *before* doing any packaging!
        'preprocess': _status_commands.Preprocess,
        'build_package_protos': _NoOpCommand,
    }
except ImportError:
    COMMAND_CLASS = {
        # wire up commands to no-op not to break the external dependencies
        'preprocess': _NoOpCommand,
        'build_package_protos': _NoOpCommand,
    }

setuptools.setup(name='grpcio-status',
                 version=grpc_version.VERSION,
                 description='Status proto mapping for gRPC',
                 long_description=open(_README_PATH, 'r').read(),
                 author='The gRPC Authors',
                 author_email='grpc-io@googlegroups.com',
                 url='https://grpc.io',
                 license='Apache License 2.0',
                 classifiers=CLASSIFIERS,
                 package_dir=PACKAGE_DIRECTORIES,
                 packages=setuptools.find_packages('.'),
                 python_requires='>=3.6',
                 install_requires=INSTALL_REQUIRES,
                 cmdclass=COMMAND_CLASS)
