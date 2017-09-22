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

def docker_for_windows_image()
  require 'digest'

  dockerfile = File.join(grpc_root, 'third_party', 'rake-compiler-dock', 'Dockerfile') 
  dockerpath = File.dirname(dockerfile)
  version = Digest::SHA1.file(dockerfile).hexdigest
  image_name = 'rake-compiler-dock_' + version
  # if "DOCKERHUB_ORGANIZATION" env is set, we try to pull the pre-built
  # rake-compiler-dock image from dockerhub rather then building from scratch.
  if ENV.has_key?('DOCKERHUB_ORGANIZATION')
    image_name = ENV['DOCKERHUB_ORGANIZATION'] + '/' + image_name
    cmd = "docker pull #{image_name}"
    puts cmd
    system cmd
    raise "Failed to pull the docker image." unless $? == 0
  else
    cmd = "docker build -t #{image_name} --file #{dockerfile} #{dockerpath}"
    puts cmd
    system cmd
    raise "Failed to build the docker image." unless $? == 0
  end
  image_name
end

def docker_for_windows(args)
  require 'rake_compiler_dock'

  args = 'bash -l' if args.empty?

  ENV['RAKE_COMPILER_DOCK_IMAGE'] = docker_for_windows_image

  RakeCompilerDock.sh args
end

if __FILE__ == $0
  docker_for_windows $*.join(' ')
end
