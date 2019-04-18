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

require 'spec_helper'

StatusCodes = GRPC::Core::StatusCodes

describe StatusCodes do
  # convert upper snake-case to camel case.
  # e.g., DEADLINE_EXCEEDED -> DeadlineExceeded
  def upper_snake_to_camel(name)
    name.to_s.split('_').map(&:downcase).map(&:capitalize).join('')
  end

  StatusCodes.constants.each do |status_name|
    it 'there is a subclass of BadStatus corresponding to StatusCode: ' \
      "#{status_name} that has code: #{StatusCodes.const_get(status_name)}" do
      camel_case = upper_snake_to_camel(status_name)
      error_class = GRPC.const_get(camel_case)
      # expect the error class to be a subclass of BadStatus
      expect(error_class < GRPC::BadStatus)

      error_object = error_class.new
      # check that the code matches the int value of the error's constant
      status_code = StatusCodes.const_get(status_name)
      expect(error_object.code).to eq(status_code)

      # check default parameters
      expect(error_object.details).to eq('unknown cause')
      expect(error_object.metadata).to eq({})

      # check that the BadStatus factory for creates the correct
      # exception too
      from_factory = GRPC::BadStatus.new_status_exception(status_code)
      expect(from_factory.is_a?(error_class)).to be(true)
    end
  end
end
