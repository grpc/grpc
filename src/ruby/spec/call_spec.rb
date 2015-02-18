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

include GRPC::Core::StatusCodes

describe GRPC::Core::RpcErrors do
  before(:each) do
    @known_types = {
      OK: 0,
      ERROR: 1,
      NOT_ON_SERVER: 2,
      NOT_ON_CLIENT: 3,
      ALREADY_ACCEPTED: 4,
      ALREADY_INVOKED: 5,
      NOT_INVOKED: 6,
      ALREADY_FINISHED: 7,
      TOO_MANY_OPERATIONS: 8,
      INVALID_FLAGS: 9,
      ErrorMessages: {
        0 => 'ok',
        1 => 'unknown error',
        2 => 'not available on a server',
        3 => 'not available on a client',
        4 => 'call is already accepted',
        5 => 'call is already invoked',
        6 => 'call is not yet invoked',
        7 => 'call is already finished',
        8 => 'outstanding read or write present',
        9 => 'a bad flag was given'
      }
    }
  end

  it 'should have symbols for all the known error codes' do
    m = GRPC::Core::RpcErrors
    syms_and_codes = m.constants.collect { |c| [c, m.const_get(c)] }
    expect(Hash[syms_and_codes]).to eq(@known_types)
  end
end

describe GRPC::Core::Call do
  before(:each) do
    @tag = Object.new
    @client_queue = GRPC::Core::CompletionQueue.new
    fake_host = 'localhost:10101'
    @ch = GRPC::Core::Channel.new(fake_host, nil)
  end

  describe '#start_read' do
    xit 'should fail if called immediately' do
      blk = proc { make_test_call.start_read(@tag) }
      expect(&blk).to raise_error GRPC::Core::CallError
    end
  end

  describe '#start_write' do
    xit 'should fail if called immediately' do
      bytes = GRPC::Core::ByteBuffer.new('test string')
      blk = proc { make_test_call.start_write(bytes, @tag) }
      expect(&blk).to raise_error GRPC::Core::CallError
    end
  end

  describe '#start_write_status' do
    xit 'should fail if called immediately' do
      blk = proc { make_test_call.start_write_status(153, 'x', @tag) }
      expect(&blk).to raise_error GRPC::Core::CallError
    end
  end

  describe '#writes_done' do
    xit 'should fail if called immediately' do
      blk = proc { make_test_call.writes_done(Object.new) }
      expect(&blk).to raise_error GRPC::Core::CallError
    end
  end

  describe '#add_metadata' do
    it 'adds metadata to a call without fail' do
      call = make_test_call
      n = 37
      one_md = proc { |x| [sprintf('key%d', x), sprintf('value%d', x)] }
      metadata = Hash[n.times.collect { |i| one_md.call i }]
      expect { call.add_metadata(metadata) }.to_not raise_error
    end
  end

  describe '#status' do
    it 'can save the status and read it back' do
      call = make_test_call
      sts = Struct::Status.new(OK, 'OK')
      expect { call.status = sts }.not_to raise_error
      expect(call.status).to eq(sts)
    end

    it 'must be set to a status' do
      call = make_test_call
      bad_sts = Object.new
      expect { call.status = bad_sts }.to raise_error(TypeError)
    end

    it 'can be set to nil' do
      call = make_test_call
      expect { call.status = nil }.not_to raise_error
    end
  end

  describe '#metadata' do
    it 'can save the metadata hash and read it back' do
      call = make_test_call
      md = { 'k1' => 'v1',  'k2' => 'v2' }
      expect { call.metadata = md }.not_to raise_error
      expect(call.metadata).to be(md)
    end

    it 'must be set with a hash' do
      call = make_test_call
      bad_md = Object.new
      expect { call.metadata = bad_md }.to raise_error(TypeError)
    end

    it 'can be set to nil' do
      call = make_test_call
      expect { call.metadata = nil }.not_to raise_error
    end
  end

  def make_test_call
    @ch.create_call('dummy_method', 'dummy_host', deadline)
  end

  def deadline
    Time.now + 2  # in 2 seconds; arbitrary
  end
end
