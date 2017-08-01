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

# GRPC contains the General RPC module.
module GRPC
  # Notifier is useful high-level synchronization primitive.
  class Notifier
    attr_reader :payload, :notified
    alias_method :notified?, :notified

    def initialize
      @mutex    = Mutex.new
      @cvar     = ConditionVariable.new
      @notified = false
      @payload  = nil
    end

    def wait
      @mutex.synchronize do
        @cvar.wait(@mutex) until notified?
      end
    end

    def notify(payload)
      @mutex.synchronize do
        return Error.new('already notified') if notified?
        @payload  = payload
        @notified = true
        @cvar.signal
        return nil
      end
    end
  end
end
