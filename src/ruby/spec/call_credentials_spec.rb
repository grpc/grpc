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

    context 'pure Ruby path only' do
      before do
        skip 'pure Ruby path only: set GRPC_EXPERIMENTS=pure_ruby_call_credentials' \
          unless GRPC::PURE_RUBY_CALL_CREDENTIALS_ENABLED
      end

      it 'can successfully create a CallCredentials from a block' do
        expect { CallCredentials.new { { 'foo' => 'bar' } } }.not_to raise_error
      end
    end

    it 'fails if initialized without a proc or block' do
      expect { CallCredentials.new('not a proc') }.to raise_error(TypeError)
    end
  end

  describe '#compose' do
    it 'can compose with another CallCredentials' do
      creds1 = CallCredentials.new(auth_proc)
      creds2 = CallCredentials.new(auth_proc)
      expect { creds1.compose(creds2) }.not_to raise_error
    end

    it 'can compose with multiple CallCredentials' do
      creds1 = CallCredentials.new(auth_proc)
      creds2 = CallCredentials.new(auth_proc)
      creds3 = CallCredentials.new(auth_proc)
      expect { creds1.compose(creds2, creds3) }.not_to raise_error
    end

    context 'pure Ruby path only' do
      before do
        skip 'pure Ruby path only: set GRPC_EXPERIMENTS=pure_ruby_call_credentials' \
          unless GRPC::PURE_RUBY_CALL_CREDENTIALS_ENABLED
      end

      it 'returns a CompositeCallCredentials with merged metadata' do
        creds1 = CallCredentials.new(auth_proc)
        creds2 = CallCredentials.new(auth_proc2)
        composite = creds1.compose(creds2)
        expect(composite).to be_a(GRPC::Core::CompositeCallCredentials)
        expect(composite.get_metadata(nil))
          .to eq({ 'plugin_key' => 'plugin_value', 'plugin_key2' => 'plugin_value2' })
      end

      it 'returns a CompositeCallCredentials when composing multiple' do
        creds1 = CallCredentials.new(auth_proc)
        creds2 = CallCredentials.new(auth_proc2)
        creds3 = CallCredentials.new(proc { { 'plugin_key3' => 'plugin_value3' } })
        composite = creds1.compose(creds2, creds3)
        expect(composite).to be_a(GRPC::Core::CompositeCallCredentials)
        expect(composite.get_metadata(nil))
          .to eq({ 'plugin_key' => 'plugin_value',
                   'plugin_key2' => 'plugin_value2',
                   'plugin_key3' => 'plugin_value3' })
      end

      it 'fails if composed with non-CallCredentials' do
        creds1 = CallCredentials.new(auth_proc)
        expect { creds1.compose('not a cred') }.to raise_error(TypeError)
      end
    end
  end
end
