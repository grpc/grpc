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

describe GRPC::Core::CompletionQueue do
  before(:example) do
    @cq = GRPC::Core::CompletionQueue.new
  end

  describe '#new' do
    it 'is constructed successufully' do
      expect { GRPC::Core::CompletionQueue.new }.not_to raise_error
    end
  end

  describe '#next' do
    it 'can be called without failing' do
      expect { @cq.next(3) }.not_to raise_error
    end

    it 'can be called with a time constant' do
      # don't use INFINITE_FUTURE, as are no events and this blocks.
      #
      # don't use INFINITE_PAST, as this fails on docker, and does not need to
      # be tested, as its not used anywhere in the ruby implementation
      a_time = GRPC::Core::TimeConsts::ZERO
      expect { @cq.next(a_time) }.not_to raise_error
    end
  end

  describe '#pluck' do
    it 'can be called without failing' do
      tag = Object.new
      expect { @cq.pluck(tag, 3) }.not_to raise_error
    end

    it 'can be called with a time constant' do
      # don't use INFINITE_FUTURE, as there no events and this blocks.
      #
      # don't use INFINITE_PAST, as this fails on docker, and does not need to
      # be tested, as its not used anywhere in the ruby implementation
      tag = Object.new
      a_time = GRPC::Core::TimeConsts::ZERO
      expect { @cq.pluck(tag, a_time) }.not_to raise_error
    end
  end
end
