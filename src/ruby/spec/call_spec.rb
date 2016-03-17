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

describe GRPC::Core::WriteFlags do
  it 'should define the known write flag values' do
    m = GRPC::Core::WriteFlags
    expect(m.const_get(:BUFFER_HINT)).to_not be_nil
    expect(m.const_get(:NO_COMPRESS)).to_not be_nil
  end
end

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

describe GRPC::Core::CallOps do
  before(:each) do
    @known_types = {
      SEND_INITIAL_METADATA: 0,
      SEND_MESSAGE: 1,
      SEND_CLOSE_FROM_CLIENT: 2,
      SEND_STATUS_FROM_SERVER: 3,
      RECV_INITIAL_METADATA: 4,
      RECV_MESSAGE: 5,
      RECV_STATUS_ON_CLIENT: 6,
      RECV_CLOSE_ON_SERVER: 7
    }
  end

  it 'should have symbols for all the known operation types' do
    m = GRPC::Core::CallOps
    syms_and_codes = m.constants.collect { |c| [c, m.const_get(c)] }
    expect(Hash[syms_and_codes]).to eq(@known_types)
  end
end

describe GRPC::Core::Call do
  let(:client_queue) { GRPC::Core::CompletionQueue.new }
  let(:test_tag)  { Object.new }
  let(:fake_host) { 'localhost:10101' }

  before(:each) do
    @ch = GRPC::Core::Channel.new(fake_host, nil, :this_channel_is_insecure)
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

  describe '#set_credentials!' do
    it 'can set a valid CallCredentials object' do
      call = make_test_call
      auth_proc = proc { { 'plugin_key' => 'plugin_value' } }
      creds = GRPC::Core::CallCredentials.new auth_proc
      expect { call.set_credentials! creds }.not_to raise_error
    end
  end

  def make_test_call
    @ch.create_call(client_queue, nil, nil, 'dummy_method', nil, deadline)
  end

  def deadline
    Time.now + 2  # in 2 seconds; arbitrary
  end
end
