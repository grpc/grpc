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

describe 'Package with underscore protobuf code generation' do
  it 'should have the same content as created by code generation' do
    root_dir = File.join(File.dirname(__FILE__), '..', '..', '..', '..', '..')
    pb_dir = File.join(root_dir, 'src', 'ruby', 'spec', 'pb')

    fail 'CONFIG env variable unexpectedly unset' unless ENV['CONFIG']
    bins_sub_dir = ENV['CONFIG']
    bins_dir = File.join(root_dir, 'bins', bins_sub_dir)

    plugin = File.join(bins_dir, 'grpc_ruby_plugin')
    protoc = File.join(bins_dir, 'protobuf', 'protoc')

    got = nil

    Dir.mktmpdir do |tmp_dir|
      gen_out = File.join(tmp_dir, 'package_with_underscore', 'service_services_pb.rb')

      pid = spawn(
        protoc,
        '-I.',
        'package_with_underscore/service.proto',
        "--grpc_out=#{tmp_dir}",
        "--plugin=protoc-gen-grpc=#{plugin}",
        chdir: pb_dir)
      Process.waitpid2(pid)
      File.open(gen_out) { |f| got = f.read }
    end

    correct_modularized_rpc = 'rpc :TestOne, '                \
      'Grpc::Testing::PackageWithUnderscore::Data::Request, ' \
      'Grpc::Testing::PackageWithUnderscore::Data::Response'
    expect(got).to include(correct_modularized_rpc)
  end
end
