# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import pkg_resources
import sys

import setuptools

from grpc_tools import protoc


def build_package_protos(package_root):
  proto_files = []
  inclusion_root = os.path.abspath(package_root)
  for root, _, files in os.walk(inclusion_root):
    for filename in files:
      if filename.endswith('.proto'):
        proto_files.append(os.path.abspath(os.path.join(root, filename)))

  well_known_protos_include = pkg_resources.resource_filename(
      'grpc_tools', '_proto')

  for proto_file in proto_files:
    command = [
        'grpc_tools.protoc',
        '--proto_path={}'.format(inclusion_root),
        '--proto_path={}'.format(well_known_protos_include),
        '--python_out={}'.format(inclusion_root),
        '--grpc_python_out={}'.format(inclusion_root),
    ] + [proto_file]
    if protoc.main(command) != 0:
      sys.stderr.write('warning: {} failed'.format(command))


class BuildPackageProtos(setuptools.Command):
  """Command to generate project *_pb2.py modules from proto files."""

  description = 'build grpc protobuf modules'
  user_options = []

  def initialize_options(self):
    pass

  def finalize_options(self):
    pass

  def run(self):
    # due to limitations of the proto generator, we require that only *one*
    # directory is provided as an 'include' directory. We assume it's the '' key
    # to `self.distribution.package_dir` (and get a key error if it's not
    # there).
    build_package_protos(self.distribution.package_dir[''])
