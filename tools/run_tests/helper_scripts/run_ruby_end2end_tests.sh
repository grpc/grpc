#!/bin/bash
# Copyright 2015 gRPC authors.
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

set -ex

# change to grpc repo root
cd "$(dirname "$0")/../../.."

EXIT_CODE=0
ruby src/ruby/end2end/sig_handling_driver.rb || EXIT_CODE=1
ruby src/ruby/end2end/channel_state_driver.rb || EXIT_CODE=1
ruby src/ruby/end2end/channel_closing_driver.rb || EXIT_CODE=1
ruby src/ruby/end2end/sig_int_during_channel_watch_driver.rb || EXIT_CODE=1
ruby src/ruby/end2end/killed_client_thread_driver.rb || EXIT_CODE=1
ruby src/ruby/end2end/forking_client_driver.rb || EXIT_CODE=1
ruby src/ruby/end2end/grpc_class_init_driver.rb || EXIT_CODE=1
ruby src/ruby/end2end/multiple_killed_watching_threads_driver.rb || EXIT_CODE=1
ruby src/ruby/end2end/load_grpc_with_gc_stress_driver.rb || EXIT_CODE=1
ruby src/ruby/end2end/client_memory_usage_driver.rb || EXIT_CODE=1
exit $EXIT_CODE
