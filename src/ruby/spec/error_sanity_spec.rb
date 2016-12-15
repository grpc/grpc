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

require 'grpc'

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
