# Copyright 2017 gRPC authors.
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

describe GRPC::InterceptorRegistry do
  let(:server) { new_rpc_server_for_testing }
  let(:interceptor) { TestServerInterceptor.new }
  let(:interceptors) { [interceptor] }
  let(:registry) { described_class.new(interceptors) }

  describe 'initialization' do
    subject { registry }

    context 'with an interceptor extending GRPC::ServerInterceptor' do
      it 'should add the interceptor to the registry' do
        subject
        is = registry.instance_variable_get('@interceptors')
        expect(is.count).to eq 1
        expect(is.first).to eq interceptor
      end
    end

    context 'with multiple interceptors' do
      let(:interceptor2) { TestServerInterceptor.new }
      let(:interceptor3) { TestServerInterceptor.new }
      let(:interceptors) { [interceptor, interceptor2, interceptor3] }

      it 'should maintain order of insertion when iterated against' do
        subject
        is = registry.instance_variable_get('@interceptors')
        expect(is.count).to eq 3
        is.each_with_index do |i, idx|
          case idx
          when 0
            expect(i).to eq interceptor
          when 1
            expect(i).to eq interceptor2
          when 2
            expect(i).to eq interceptor3
          end
        end
      end
    end

    context 'with an interceptor not extending GRPC::ServerInterceptor' do
      let(:interceptor) { Class }
      let(:err) { GRPC::InterceptorRegistry::DescendantError }

      it 'should raise an InvalidArgument exception' do
        expect { subject }.to raise_error(err)
      end
    end
  end
end
