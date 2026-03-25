# frozen_string_literal: true

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

describe GRPC::Core::CallCredentials do
  CallCredentials = GRPC::Core::CallCredentials
  let(:auth_proc) { proc { { 'plugin_key' => 'plugin_value' } } }
  let(:auth_proc2) { proc { { 'plugin_key2' => 'plugin_value2' } } }

  describe '#new' do
    it 'can successfully create a CallCredentials from a proc' do
      expect { CallCredentials.new(auth_proc) }.not_to raise_error
    end

    it 'can successfully create a CallCredentials from a block' do
      expect { CallCredentials.new { { 'foo' => 'bar' } } }.not_to raise_error
    end

    it 'fails if initialized without a proc or block' do
      expect { CallCredentials.new('not a proc') }.to raise_error(TypeError)
    end
  end

  describe '#compose' do
    it 'can compose with another CallCredentials' do
      creds1 = CallCredentials.new(auth_proc)
      creds2 = CallCredentials.new(auth_proc2)
      composite = creds1.compose(creds2)
      expect(composite).to be_a(GRPC::Core::CompositeCallCredentials)
      expect(composite.get_metadata(nil)).to eq({ 'plugin_key' => 'plugin_value', 'plugin_key2' => 'plugin_value2' })
    end

    it 'can compose with multiple CallCredentials' do
      creds1 = CallCredentials.new(auth_proc)
      creds2 = CallCredentials.new(auth_proc2)
      creds3 = CallCredentials.new(proc { { 'plugin_key3' => 'plugin_value3' } })
      composite = creds1.compose(creds2, creds3)
      expect(composite).to be_a(GRPC::Core::CompositeCallCredentials)
      expect(composite.get_metadata(nil)).to eq({ 'plugin_key' => 'plugin_value', 'plugin_key2' => 'plugin_value2', 'plugin_key3' => 'plugin_value3' })
    end

    it 'fails if composed with non-CallCredentials' do
      creds1 = CallCredentials.new(auth_proc)
      expect { creds1.compose('not a cred') }.to raise_error(TypeError)
    end
  end

  describe GRPC::Core::CallCredentialsHelper do
    describe '.resolve' do
      it 'returns nil if both are nil' do
        expect(GRPC::Core::CallCredentialsHelper.resolve(nil, nil)).to eq(nil)
      end

      it 'returns channel creds if call creds are nil' do
        creds = double('creds')
        expect(GRPC::Core::CallCredentialsHelper.resolve(creds, nil)).to eq(creds)
      end

      it 'returns call creds if channel creds are nil' do
        creds = double('creds')
        expect(GRPC::Core::CallCredentialsHelper.resolve(nil, creds)).to eq(creds)
      end

      it 'composes if both are present' do
        creds1 = double('creds1')
        creds2 = double('creds2')
        expect(creds1).to receive(:compose).with(creds2).and_return('composite')
        expect(GRPC::Core::CallCredentialsHelper.resolve(creds1, creds2)).to eq('composite')
      end
    end

    describe '.apply' do
      let(:metadata) { {} }
      let(:creds) { double('creds') }

      it 'does nothing if creds are nil' do
        GRPC::Core::CallCredentialsHelper.apply(nil, metadata, 'host', 'channel_creds')
        expect(metadata).to eq({})
      end

      it 'does nothing if channel is insecure' do
        GRPC::Core::CallCredentialsHelper.apply(creds, metadata, 'host', :this_channel_is_insecure)
        expect(metadata).to eq({})
      end

      it 'applies metadata if valid' do
        expect(creds).to receive(:get_metadata).and_return({ 'foo' => 'bar' })
        GRPC::Core::CallCredentialsHelper.apply(creds, metadata, 'host', 'secure_channel_creds')
        expect(metadata).to eq({ 'foo' => 'bar' })
      end

      it 'handles exceptions' do
        expect(creds).to receive(:get_metadata).and_raise('error')
        expect do
          GRPC::Core::CallCredentialsHelper.apply(creds, metadata, 'host', 'secure_channel_creds')
        end.to raise_error(GRPC::BadStatus)
      end

      it 'converts keys and values to strings' do
        expect(creds).to receive(:get_metadata).and_return({ foo: :bar })
        GRPC::Core::CallCredentialsHelper.apply(creds, metadata, 'host', 'secure_channel_creds')
        expect(metadata).to eq({ 'foo' => 'bar' })
      end
    end
  end
end
