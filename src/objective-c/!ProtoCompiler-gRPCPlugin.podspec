# This file has been automatically generated from a template file.
# Please make modifications to
# `templates/src/objective-c/!ProtoCompiler-gRPCPlugin.podspec.template`
# instead. This file can be regenerated from the template by running
# `tools/buildgen/generate_projects.sh`.

# CocoaPods podspec for the gRPC Proto Compiler Plugin
#
# Copyright 2016, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

Pod::Spec.new do |s|
  # This pod is only a utility that will be used by other pods _at install time_ (not at compile
  # time). Other pods can access it in their `prepare_command` script, under <pods_root>/<pod name>.
  # Because CocoaPods installs pods in alphabetical order, beginning this pod's name with an
  # exclamation mark ensures that other "regular" pods will be able to find it as it'll be installed
  # before them.
  s.name     = '!ProtoCompiler-gRPCPlugin'
  v = '1.2.0-dev'
  s.version  = v
  s.summary  = 'The gRPC ProtoC plugin generates Objective-C files from .proto services.'
  s.description = <<-DESC
    This podspec only downloads the gRPC protoc plugin so that local pods generating protos can use
    it in their invocation of protoc, as part of their prepare_command.
    The generated code will have a dependency on the gRPC Objective-C Proto runtime of the same
    version. The runtime can be obtained as the "gRPC-ProtoRPC" pod.
  DESC
  s.homepage = 'http://www.grpc.io'
  s.license  = {
    :type => 'New BSD',
    :text => <<-LICENSE
      Copyright 2015, gRPC authors

      Licensed under the Apache License, Version 2.0 (the "License");
      you may not use this file except in compliance with the License.
      You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

      Unless required by applicable law or agreed to in writing, software
      distributed under the License is distributed on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
      See the License for the specific language governing permissions and
      limitations under the License.
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
  plugin = 'grpc_objective_c_plugin'

  s.preserve_paths = plugin

  # Restrict the protoc version to the one supported by this plugin.
  s.dependency '!ProtoCompiler', '3.1.0'
  # For the Protobuf dependency not to complain:
  s.ios.deployment_target = '7.1'
  s.osx.deployment_target = '10.9'
  # Restrict the gRPC runtime version to the one supported by this plugin.
  s.dependency 'gRPC-ProtoRPC', v

  # This is only for local development of the plugin: If the Podfile brings this pod from a local
  # directory using `:path`, CocoaPods won't download the zip file and so the plugin won't be
  # present in this pod's directory. We use that knowledge to check for the existence of the file
  # and, if absent, compile the plugin from the local sources.
  s.prepare_command = <<-CMD
    if [ ! -f #{plugin} ]; then
      cd #{repo_root}
      # This will build the plugin and put it in #{repo_root}/bins/opt.
      #
      # TODO(jcanizales): I reckon make will try to use locally-installed libprotoc (headers and
      # library binary) if found, which _we do not want_. Find a way for this to always use the
      # sources in the repo.
      make #{plugin}
      cd -
    fi
  CMD
end
