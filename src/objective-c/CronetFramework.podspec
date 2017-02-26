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
  s.name         = "CronetFramework"
  v = '0.0.4'
  s.version      = v
  s.summary      = "Cronet, precompiled and used as a framework."
  s.homepage     = "http://chromium.org"
  s.license      = {
    :type => 'BSD',
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
  s.vendored_framework = "Cronet.framework"
  s.author             = "The Chromium Authors"
  s.ios.deployment_target = "8.0"
  s.source       = { :http => "https://storage.googleapis.com/grpc-precompiled-binaries/cronet/Cronet.framework-v#{v}.zip"}
  s.preserve_paths = "Cronet.framework"
  s.public_header_files = "Cronet.framework/Headers/**/*{.h}"
  s.source_files = "Cronet.framework/Headers/**/*{.h}"
end
