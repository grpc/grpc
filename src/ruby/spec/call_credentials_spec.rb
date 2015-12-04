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

describe GRPC::Core::CallCredentials do
  CallCredentials = GRPC::Core::CallCredentials

  let(:auth_proc) { proc { { 'plugin_key' => 'plugin_value' } } }

  describe '#new' do
    it 'can successfully create a CallCredentials from a proc' do
      expect { CallCredentials.new(auth_proc) }.not_to raise_error
    end
  end

  describe '#compose' do
    it 'can compose with another CallCredentials' do
      creds1 = CallCredentials.new(auth_proc)
      creds2 = CallCredentials.new(auth_proc)
      expect { creds1.compose creds2 }.not_to raise_error
    end

    it 'can compose with multiple CallCredentials' do
      creds1 = CallCredentials.new(auth_proc)
      creds2 = CallCredentials.new(auth_proc)
      creds3 = CallCredentials.new(auth_proc)
      expect { creds1.compose(creds2, creds3) }.not_to raise_error
    end
  end
end
