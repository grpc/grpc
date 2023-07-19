# This file has been automatically generated from a template file.
# Please make modifications to
# `templates/src/objective-c/!ProtoCompiler-gRPCPlugin.podspec.template`
# instead. This file can be regenerated from the template by running
# `tools/buildgen/generate_projects.sh`.

# CocoaPods podspec for the gRPC Proto Compiler Plugin
#
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

Pod::Spec.new do |s|
  # This pod is only a utility that will be used by other pods _at install time_ (not at compile
  # time). Other pods can access it in their `prepare_command` script, under <pods_root>/<pod name>.
  # Because CocoaPods installs pods in alphabetical order, beginning this pod's name with an
  # exclamation mark ensures that other "regular" pods will be able to find it as it'll be installed
  # before them.
  s.name     = '!ProtoCompiler-gRPCPlugin'
  v = '1.57.0-dev'
  s.version  = v
  s.summary  = 'The gRPC ProtoC plugin generates Objective-C files from .proto services.'
  s.description = <<-DESC
    This podspec only downloads the gRPC protoc plugin so that local pods generating protos can use
    it in their invocation of protoc, as part of their prepare_command.
    The generated code will have a dependency on the gRPC Objective-C Proto runtime of the same
    version. The runtime can be obtained as the "gRPC-ProtoRPC" pod.
  DESC
  s.homepage = 'https://grpc.io'
  s.license  = {
    :type => 'Apache License, Version 2.0',
    :text => <<-LICENSE
      Copyright 2015, Google Inc.
      All rights reserved.

      Redistribution and use in source and binary forms, with or without
      modification, are permitted provided that the following conditions are
      met:

          * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
          * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following disclaimer
      in the documentation and/or other materials provided with the
      distribution.
          * Neither the name of Google Inc. nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
      "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
      LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
      A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
      OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
      SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
      LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
      DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
      THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
      (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
      OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
    LICENSE
  }
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  repo = 'grpc/grpc'
  file = "grpc_objective_c_plugin-#{v}-macos-x86_64.zip"
  s.source = {
    :http => "https://github.com/#{repo}/releases/download/v#{v}/#{file}",
    # TODO(jcanizales): Add sha1 or sha256
    # :sha1 => '??',
  }

  repo_root = '../..'
  bazel = "#{repo_root}/tools/bazel"
  plugin = 'grpc_objective_c_plugin'

  s.preserve_paths = plugin

  # Restrict the protoc version to the one supported by this plugin.
  s.dependency '!ProtoCompiler', '3.24.0-rc2'

  s.ios.deployment_target = '10.0'
  s.osx.deployment_target = '10.12'
  s.tvos.deployment_target = '12.0'
  s.watchos.deployment_target = '6.0'

  # Restrict the gRPC runtime version to the one supported by this plugin.
  s.dependency 'gRPC-ProtoRPC', v

  # This is only for local development of the plugin: If the Podfile brings this pod from a local
  # directory using `:path`, CocoaPods won't download the zip file and so the plugin won't be
  # present in this pod's directory. We use that knowledge to check for the existence of the file
  # and, if absent, compile the plugin from the local sources.
  s.prepare_command = <<-CMD
    set -e
    if [ ! -f #{plugin} ]; then
      #{bazel} build //src/compiler:grpc_objective_c_plugin
    fi
  CMD
end
