# Copyright 2015, Google Inc.
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

require_relative '../grpc'

# GRPC contains the General RPC module.
module GRPC
  module Core
    # TimeConsts is a module from the C extension.
    #
    # Here it's re-opened to add a utility func.
    module TimeConsts
      # Converts a time delta to an absolute deadline.
      #
      # Assumes timeish is a relative time, and converts its to an absolute,
      # with following exceptions:
      #
      # * if timish is one of the TimeConsts.TimeSpec constants the value is
      # preserved.
      # * timish < 0 => TimeConsts.INFINITE_FUTURE
      # * timish == 0 => TimeConsts.ZERO
      #
      # @param timeish [Number|TimeSpec]
      # @return timeish [Number|TimeSpec]
      def from_relative_time(timeish)
        if timeish.is_a? TimeSpec
          timeish
        elsif timeish.nil?
          TimeConsts::ZERO
        elsif !timeish.is_a? Numeric
          fail(TypeError,
               "Cannot make an absolute deadline from #{timeish.inspect}")
        elsif timeish < 0
          TimeConsts::INFINITE_FUTURE
        elsif timeish.zero?
          TimeConsts::ZERO
        else
          Time.now + timeish
        end
      end

      module_function :from_relative_time
    end
  end
end
