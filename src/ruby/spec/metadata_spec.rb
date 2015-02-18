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

describe GRPC::Core::Metadata do
  describe '#new' do
    it 'should create instances' do
      expect { GRPC::Core::Metadata.new('a key', 'a value') }.to_not raise_error
    end
  end

  describe '#key' do
    md = GRPC::Core::Metadata.new('a key', 'a value')
    it 'should be the constructor value' do
      expect(md.key).to eq('a key')
    end
  end

  describe '#value' do
    md = GRPC::Core::Metadata.new('a key', 'a value')
    it 'should be the constuctor value' do
      expect(md.value).to eq('a value')
    end
  end

  describe '#dup' do
    it 'should create a copy that returns the correct key' do
      md = GRPC::Core::Metadata.new('a key', 'a value')
      expect(md.dup.key).to eq('a key')
    end

    it 'should create a copy that returns the correct value' do
      md = GRPC::Core::Metadata.new('a key', 'a value')
      expect(md.dup.value).to eq('a value')
    end
  end
end
