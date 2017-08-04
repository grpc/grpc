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
