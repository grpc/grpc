# Copyright 2014, Google Inc.
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


describe GRPC::Core::StatusCodes do

  StatusCodes = GRPC::Core::StatusCodes

  before(:each) do
    @known_types = {
      :OK => 0,
      :CANCELLED => 1,
      :UNKNOWN => 2,
      :INVALID_ARGUMENT => 3,
      :DEADLINE_EXCEEDED => 4,
      :NOT_FOUND => 5,
      :ALREADY_EXISTS => 6,
      :PERMISSION_DENIED => 7,
      :RESOURCE_EXHAUSTED => 8,
      :FAILED_PRECONDITION => 9,
      :ABORTED => 10,
      :OUT_OF_RANGE => 11,
      :UNIMPLEMENTED => 12,
      :INTERNAL => 13,
      :UNAVAILABLE => 14,
      :DATA_LOSS => 15,
      :UNAUTHENTICATED => 16
    }
  end

  it 'should have symbols for all the known status codes' do
    m = StatusCodes
    syms_and_codes = m.constants.collect { |c| [c, m.const_get(c)] }
    expect(Hash[syms_and_codes]).to eq(@known_types)
  end

end


describe GRPC::Core::Status do

  Status = GRPC::Core::Status

  describe '#new' do
    it 'should create new instances' do
      expect { Status.new(142, 'test details') }.to_not raise_error
    end
  end

  describe '#details' do
    it 'return the detail' do
      sts = Status.new(142, 'test details')
      expect(sts.details).to eq('test details')
    end
  end

  describe '#code' do
    it 'should return the code' do
      sts = Status.new(142, 'test details')
      expect(sts.code).to eq(142)
    end
  end

  describe '#dup' do
    it 'should create a copy that returns the correct details' do
      sts = Status.new(142, 'test details')
      expect(sts.dup.code).to eq(142)
    end

    it 'should create a copy that returns the correct code' do
      sts = Status.new(142, 'test details')
      expect(sts.dup.details).to eq('test details')
    end
  end


end


describe GRPC::BadStatus do

  BadStatus = GRPC::BadStatus

  describe '#new' do
    it 'should create new instances' do
      expect { BadStatus.new(142, 'test details') }.to_not raise_error
    end
  end

  describe '#details' do
    it 'return the detail' do
      err = BadStatus.new(142, 'test details')
      expect(err.details).to eq('test details')
    end
  end

  describe '#code' do
    it 'should return the code' do
      err = BadStatus.new(142, 'test details')
      expect(err.code).to eq(142)
    end
  end

  describe '#dup' do
    it 'should create a copy that returns the correct details' do
      err = BadStatus.new(142, 'test details')
      expect(err.dup.code).to eq(142)
    end

    it 'should create a copy that returns the correct code' do
      err = BadStatus.new(142, 'test details')
      expect(err.dup.details).to eq('test details')
    end
  end

  describe '#to_status' do
    it 'should create a Status with the same code and details' do
      err = BadStatus.new(142, 'test details')
      sts = err.to_status
      expect(sts.code).to eq(142)
      expect(sts.details).to eq('test details')
    end

    it 'should create a copy that returns the correct code' do
      err = BadStatus.new(142, 'test details')
      expect(err.dup.details).to eq('test details')
    end
  end

  describe 'as an exception' do

    it 'can be raised' do
      blk = Proc.new { raise BadStatus.new(343, 'status 343') }
      expect(&blk).to raise_error(BadStatus)
    end
  end

end
