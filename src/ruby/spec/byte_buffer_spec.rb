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

describe GRPC::Core::ByteBuffer do
  describe '#new' do
    it 'is constructed from a string' do
      expect { GRPC::Core::ByteBuffer.new('#new') }.not_to raise_error
    end

    it 'can be constructed from the empty string' do
      expect { GRPC::Core::ByteBuffer.new('') }.not_to raise_error
    end

    it 'cannot be constructed from nil' do
      expect { GRPC::Core::ByteBuffer.new(nil) }.to raise_error TypeError
    end

    it 'cannot be constructed from non-strings' do
      [1, Object.new, :a_symbol].each do |x|
        expect { GRPC::Core::ByteBuffer.new(x) }.to raise_error TypeError
      end
    end
  end

  describe '#to_s' do
    it 'is the string value the ByteBuffer was constructed with' do
      expect(GRPC::Core::ByteBuffer.new('#to_s').to_s).to eq('#to_s')
    end
  end

  describe '#dup' do
    it 'makes an instance whose #to_s is the original string value' do
      bb = GRPC::Core::ByteBuffer.new('#dup')
      a_copy = bb.dup
      expect(a_copy.to_s).to eq('#dup')
      expect(a_copy.dup.to_s).to eq('#dup')
    end
  end
end
