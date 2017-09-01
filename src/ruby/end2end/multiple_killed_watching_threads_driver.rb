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

require_relative './end2end_common'

Thread.abort_on_exception = true

include GRPC::Core::ConnectivityStates

def watch_state(ch)
  thd = Thread.new do
    state = ch.connectivity_state(false)
    fail "non-idle state: #{state}" unless state == IDLE
    ch.watch_connectivity_state(IDLE, Time.now + 360)
  end
  sleep 0.1
  thd.kill
end

def main
  channels = []
  10.times do
    ch = GRPC::Core::Channel.new('dummy_host',
                                 nil, :this_channel_is_insecure)
    watch_state(ch)
    channels << ch
  end

  # checking state should still be safe to call
  channels.each do |c|
    fail unless c.connectivity_state(false) == FATAL_FAILURE
  end
end

main
