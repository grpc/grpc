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

require 'grpc'

def load_test_certs
  test_root = File.join(File.dirname(__FILE__), 'testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

describe GRPC::Core::Channel do
  let(:fake_host) { 'localhost:0' }

  def create_test_cert
    GRPC::Core::ChannelCredentials.new(load_test_certs[0])
  end

  shared_examples '#new' do
    it 'take a host name without channel args' do
      blk = proc do
        GRPC::Core::Channel.new('dummy_host', nil, :this_channel_is_insecure)
      end
      expect(&blk).not_to raise_error
    end

    it 'does not take a hash with bad keys as channel args' do
      blk = construct_with_args(Object.new => 1)
      expect(&blk).to raise_error TypeError
      blk = construct_with_args(1 => 1)
      expect(&blk).to raise_error TypeError
    end

    it 'does not take a hash with bad values as channel args' do
      blk = construct_with_args(symbol: Object.new)
      expect(&blk).to raise_error TypeError
      blk = construct_with_args('1' => {})
      expect(&blk).to raise_error TypeError
    end

    it 'can take a hash with a symbol key as channel args' do
      blk = construct_with_args(a_symbol: 1)
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with a string key as channel args' do
      blk = construct_with_args('a_symbol' => 1)
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with a string value as channel args' do
      blk = construct_with_args(a_symbol: '1')
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with a symbol value as channel args' do
      blk = construct_with_args(a_symbol: :another_symbol)
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with a numeric value as channel args' do
      blk = construct_with_args(a_symbol: 1)
      expect(&blk).to_not raise_error
    end

    it 'can take a hash with many args as channel args' do
      args = Hash[127.times.collect { |x| [x.to_s, x] }]
      blk = construct_with_args(args)
      expect(&blk).to_not raise_error
    end
  end

  describe '#new for secure channels' do
    def construct_with_args(a)
      proc { GRPC::Core::Channel.new('dummy_host', a, create_test_cert) }
    end

    it_behaves_like '#new'
  end

  describe '#new for insecure channels' do
    it_behaves_like '#new'

    def construct_with_args(a)
      proc do
        GRPC::Core::Channel.new('dummy_host', a, :this_channel_is_insecure)
      end
    end
  end

  describe '#create_call' do
    it 'creates a call OK' do
      ch = GRPC::Core::Channel.new(fake_host, nil, :this_channel_is_insecure)

      deadline = Time.now + 5

      blk = proc do
        ch.create_call(nil, nil, 'dummy_method', nil, deadline)
      end
      expect(&blk).to_not raise_error
    end

    it 'raises an error if called on a closed channel' do
      ch = GRPC::Core::Channel.new(fake_host, nil, :this_channel_is_insecure)
      ch.close

      deadline = Time.now + 5
      blk = proc do
        ch.create_call(nil, nil, 'dummy_method', nil, deadline)
      end
      expect(&blk).to raise_error(RuntimeError)
    end
  end

  describe '#destroy' do
    it 'destroys a channel ok' do
      ch = GRPC::Core::Channel.new(fake_host, nil, :this_channel_is_insecure)
      blk = proc { ch.destroy }
      expect(&blk).to_not raise_error
    end

    it 'can be called more than once without error' do
      ch = GRPC::Core::Channel.new(fake_host, nil, :this_channel_is_insecure)
      blk = proc { ch.destroy }
      blk.call
      expect(&blk).to_not raise_error
    end
  end

  describe '#connectivity_state' do
    it 'returns an enum' do
      ch = GRPC::Core::Channel.new(fake_host, nil, :this_channel_is_insecure)
      valid_states = [
        GRPC::Core::ConnectivityStates::IDLE,
        GRPC::Core::ConnectivityStates::CONNECTING,
        GRPC::Core::ConnectivityStates::READY,
        GRPC::Core::ConnectivityStates::TRANSIENT_FAILURE,
        GRPC::Core::ConnectivityStates::FATAL_FAILURE
      ]

      expect(valid_states).to include(ch.connectivity_state)
    end

    it 'returns an enum when trying to connect' do
      ch = GRPC::Core::Channel.new(fake_host, nil, :this_channel_is_insecure)
      ch.connectivity_state(true)
      valid_states = [
        GRPC::Core::ConnectivityStates::IDLE,
        GRPC::Core::ConnectivityStates::CONNECTING,
        GRPC::Core::ConnectivityStates::READY,
        GRPC::Core::ConnectivityStates::TRANSIENT_FAILURE,
        GRPC::Core::ConnectivityStates::FATAL_FAILURE
      ]

      expect(valid_states).to include(ch.connectivity_state)
    end
  end

  describe '::SSL_TARGET' do
    it 'is a symbol' do
      expect(GRPC::Core::Channel::SSL_TARGET).to be_a(Symbol)
    end
  end

  describe '#close' do
    it 'closes a channel ok' do
      ch = GRPC::Core::Channel.new(fake_host, nil, :this_channel_is_insecure)
      blk = proc { ch.close }
      expect(&blk).to_not raise_error
    end

    it 'can be called more than once without error' do
      ch = GRPC::Core::Channel.new(fake_host, nil, :this_channel_is_insecure)
      blk = proc { ch.close }
      blk.call
      expect(&blk).to_not raise_error
    end
  end
end
