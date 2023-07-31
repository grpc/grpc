#!/usr/bin/env ruby
#
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

ENV['GRPC_ENABLE_FORK_SUPPORT'] = "1"
fail "forking only supported on linux" unless RUBY_PLATFORM =~ /linux/

this_dir = File.expand_path(File.dirname(__FILE__))
protos_lib_dir = File.join(this_dir, 'lib')
grpc_lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(grpc_lib_dir) unless $LOAD_PATH.include?(grpc_lib_dir)
$LOAD_PATH.unshift(protos_lib_dir) unless $LOAD_PATH.include?(protos_lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'
require 'end2end_common'

def main
  10_000.times do
    # The prefork and postfork APIs are meant to be used before and after a
    # fork. So this is not technically correct usage of the API. However, the
    # current implementation doesn't actually care about a "fork" call happening
    # in between prefork and postfork_parent, and this is unlikely to change anytime
    # soon. Also note the goal of this test is mainly to test the background thread startup
    # and shutdown that happens in prefork and postfork_parent. If we were to actually
    # fork in this test, it would take much longer to run.
    GRPC.prefork
    GRPC.postfork_parent
  end
end

main
