# Copyright 2016 gRPC authors.
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
