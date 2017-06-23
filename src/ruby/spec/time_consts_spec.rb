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

TimeConsts = GRPC::Core::TimeConsts

describe TimeConsts do
  before(:each) do
    @known_consts = [:ZERO, :INFINITE_FUTURE, :INFINITE_PAST].sort
  end

  it 'should have all the known types' do
    expect(TimeConsts.constants.collect.sort).to eq(@known_consts)
  end

  describe '#to_time' do
    it 'converts each constant to a Time' do
      m = TimeConsts
      m.constants.each do |c|
        expect(m.const_get(c).to_time).to be_a(Time)
      end
    end
  end
end

describe '#from_relative_time' do
  it 'cannot handle arbitrary objects' do
    expect { TimeConsts.from_relative_time(Object.new) }.to raise_error
  end

  it 'preserves TimeConsts' do
    m = TimeConsts
    m.constants.each do |c|
      const = m.const_get(c)
      expect(TimeConsts.from_relative_time(const)).to be(const)
    end
  end

  it 'converts 0 to TimeConsts::ZERO' do
    expect(TimeConsts.from_relative_time(0)).to eq(TimeConsts::ZERO)
  end

  it 'converts nil to TimeConsts::ZERO' do
    expect(TimeConsts.from_relative_time(nil)).to eq(TimeConsts::ZERO)
  end

  it 'converts negative values to TimeConsts::INFINITE_FUTURE' do
    [-1, -3.2, -1e6].each do |t|
      y = TimeConsts.from_relative_time(t)
      expect(y).to eq(TimeConsts::INFINITE_FUTURE)
    end
  end

  it 'converts a positive value to an absolute time' do
    epsilon = 1
    [1, 3.2, 1e6].each do |t|
      want = Time.now + t
      abs = TimeConsts.from_relative_time(t)
      expect(abs.to_f).to be_within(epsilon).of(want.to_f)
    end
  end
end
