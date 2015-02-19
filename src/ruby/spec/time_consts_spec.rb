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
