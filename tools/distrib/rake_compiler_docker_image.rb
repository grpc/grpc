#!/usr/bin/env ruby
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

def grpc_root()
  File.expand_path(File.join(File.dirname(__FILE__), '..', '..'))
end

def docker_image_for_rake_compiler(platform)
  require 'digest'

  dockerfile_dir = File.join(grpc_root, 'third_party', 'rake-compiler-dock', 'rake_' + platform)
  # the ".current_version" file contains image name, tag and sha256 digest
  current_version_file = dockerfile_dir + '.current_version'
  image_name = File.read(current_version_file)
end

def run_rake_compiler(platform, args)
  require 'rake_compiler_dock'

  ENV['RCD_RUBYVM'] = 'mri'
  ENV['RCD_PLATFORM'] = platform
  ENV['RCD_IMAGE'] = docker_image_for_rake_compiler(platform)
  RakeCompilerDock.sh args
end
