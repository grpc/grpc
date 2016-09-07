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

require 'open3'
require 'tmpdir'

def can_run_codegen_check
  system('which grpc_ruby_plugin') && system('which protoc')
end

describe 'Ping protobuf code generation' do
  if !can_run_codegen_check
    skip 'protoc || grpc_ruby_plugin missing, cannot verify ping code-gen'
  else
    it 'should have the same content as created by code generation' do
      root_dir = File.join(File.dirname(__FILE__), '..', '..', '..', '..', '..')

      # Get the current content
      service_path = File.join(root_dir, 'src', 'ruby', 'pb', 'grpc',
                               'testing', 'duplicate',
                               'echo_duplicate_services_pb.rb')
      want = nil
      File.open(service_path) { |f| want = f.read }

      # Regenerate it
      plugin, = Open3.capture2('which', 'grpc_ruby_plugin')
      plugin = plugin.strip
      got = nil
      Dir.mktmpdir do |tmp_dir|
        gen_out = File.join(tmp_dir, 'src', 'proto', 'grpc', 'testing',
                            'duplicate', 'echo_duplicate_services_pb.rb')
        pid = spawn(
          'protoc',
          '-I.',
          'src/proto/grpc/testing/duplicate/echo_duplicate.proto',
          "--grpc_out=#{tmp_dir}",
          "--plugin=protoc-gen-grpc=#{plugin}",
          chdir: root_dir)
        Process.wait(pid)
        File.open(gen_out) { |f| got = f.read }
      end
      expect(got).to eq(want)
    end
  end
end
