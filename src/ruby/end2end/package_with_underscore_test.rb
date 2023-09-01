# Copyright 2018 gRPC authors.
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

def main
  root_dir = File.join(File.dirname(__FILE__), '..', '..', '..')
  pb_dir = File.join(root_dir, 'src', 'ruby', 'end2end', 'protos')

  bins_dir = File.join(root_dir, 'cmake', 'build')
  plugin = File.join(bins_dir, 'grpc_ruby_plugin')
  protoc = File.join(bins_dir, 'third_party', 'protobuf', 'protoc')

  got = nil

  Dir.mktmpdir do |tmp_dir|
    gen_out = File.join(tmp_dir, 'package_with_underscore', 'service_services_pb.rb')

    pid = spawn(
      protoc,
      "--proto_path=#{pb_dir}",
      'package_with_underscore/service.proto',
      "--grpc_out=#{tmp_dir}",
      "--plugin=protoc-gen-grpc=#{plugin}"
    )
    Process.waitpid2(pid)
    File.open(gen_out) { |f| got = f.read }
  end

  correct_modularized_rpc = 'rpc :TestOne, ' \
                            '::Grpc::Testing::PackageWithUnderscore::Data::Request, ' \
                            '::Grpc::Testing::PackageWithUnderscore::Data::Response'

  return if got.include?(correct_modularized_rpc)

  fail 'generated file does not match with correct_modularized_rpc'
end

main
