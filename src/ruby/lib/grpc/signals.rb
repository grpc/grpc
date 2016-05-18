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

require 'thread'
require_relative 'grpc'

# GRPC contains the General RPC module.
module GRPC
  # Signals contains gRPC functions related to signal handling
  module Signals
    @signal_handlers = []
    @handlers_mutex = Mutex.new
    @previous_handlers = {}
    # @signal_received = false

    def register_handler(&handler)
      @handlers_mutex.synchronize do
        @signal_handlers.push(handler)
        # handler.call if @signal_received
      end
      # Returns a function to remove the handler
      lambda do
        @handlers_mutex.synchronize { @signal_handlers.delete(handler) }
      end
    end
    module_function :register_handler

    def run_handlers(signal)
      # @signal_received = true
      @signal_handlers.each(&:call)
      @previous_handlers[signal].call
    end
    module_function :run_handlers

    def init
      %w(INT TERM).each do |sig|
        @previous_handlers[sig] = Signal.trap(sig) { run_handlers(sig) }
      end
    end
    module_function :init
  end
end
