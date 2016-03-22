#!/usr/bin/env ruby
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

def grpc_root()
  File.expand_path(File.join(File.dirname(__FILE__), '..', '..'))
end

def docker_for_windows_image()
  require 'digest'

  dockerfile = File.join(grpc_root, 'third_party', 'rake-compiler-dock', 'Dockerfile') 
  dockerpath = File.dirname(dockerfile)
  version = Digest::SHA1.file(dockerfile).hexdigest
  image_name = 'grpc/rake-compiler-dock:' + version
  cmd = "docker build -t #{image_name} --file #{dockerfile} #{dockerpath}"
  puts cmd
  system cmd
  raise "Failed to build the docker image." unless $? == 0
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
