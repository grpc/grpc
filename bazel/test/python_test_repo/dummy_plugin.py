# Copyright 2019 the gRPC authors.
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
"""A dummy plugin for testing"""

import sys

from google.protobuf.compiler.plugin_pb2 import CodeGeneratorRequest
from google.protobuf.compiler.plugin_pb2 import CodeGeneratorResponse


def main(input_file=sys.stdin, output_file=sys.stdout):
    request = CodeGeneratorRequest.FromString(input_file.buffer.read())
    answer = []
    for fname in request.file_to_generate:
        answer.append(CodeGeneratorResponse.File(
            name=fname.replace('.proto', '_pb2.py'),
            insertion_point='module_scope',
            content="# Hello {}, I'm a dummy plugin!".format(fname),
        ))

    cgr = CodeGeneratorResponse(file=answer)
    output_file.buffer.write(cgr.SerializeToString())


if __name__ == '__main__':
    main()
