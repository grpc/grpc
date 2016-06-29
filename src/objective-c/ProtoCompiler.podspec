# BoringSSL CocoaPods podspec

# Copyright 2015, Google Inc.
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

Pod::Spec.new do |s|
  s.name     = 'ProtoCompiler'
  v = '3.0.0-beta-3.1'
  s.version  = v
  s.summary  = 'The Protobuf Compiler (protoc) generates Objective-C files from .proto files'
  s.description = <<-DESC
    This podspec only downloads protoc so that local pods generating protos can execute it as part
    of their prepare_command.
    The generated code will have a dependency on the Protobuf Objective-C runtime of the same
    version. The runtime can be obtained as the "Protobuf" pod.
  DESC
  s.homepage = 'https://github.com/google/protobuf'
  s.license  = 'New BSD'
  # "The name and email addresses of the library maintainers, not the Podspec maintainer."
  s.authors  = { 'The Protocol Buffers contributors' => 'protobuf@googlegroups.com' }

  s.source = {
    :http => "https://github.com/google/protobuf/releases/download/v#{v}/protoc-#{v}-osx-fat.zip",
    # TODO(jcanizales): Add sha1 or sha256
    # :sha1 => '??',
  }

  s.preserve_paths = 'protoc',
                     'google/**/*.proto' # Well-known protobuf types
end
