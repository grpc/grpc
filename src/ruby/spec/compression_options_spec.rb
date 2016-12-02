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

describe GRPC::Core::CompressionOptions do
  # Note these constants should be updated
  # according to what the core lib provides.

  # Names of supported compression algorithms
  ALGORITHMS = [:identity, :deflate, :gzip]

  # Names of valid supported compression levels
  COMPRESS_LEVELS = [:none, :low, :medium, :high]

  it 'implements to_s' do
    expect { GRPC::Core::CompressionOptions.new.to_s }.to_not raise_error
  end

  it '#to_channel_arg_hash gives the same result as #to_hash' do
    options = GRPC::Core::CompressionOptions.new
    expect(options.to_channel_arg_hash).to eq(options.to_hash)
  end

  # Test the normal call sequence of creating an instance
  # and then obtaining the resulting channel-arg hash that
  # corresponds to the compression settings of the instance
  describe 'creating, reading, and converting to channel args hash' do
    it 'works when no optional args were provided' do
      options = GRPC::Core::CompressionOptions.new

      ALGORITHMS.each do |algorithm|
        expect(options.algorithm_enabled?(algorithm)).to be true
      end

      expect(options.disabled_algorithms).to be_empty
      expect(options.default_algorithm).to be nil
      expect(options.default_level).to be nil
      expect(options.to_hash).to be_instance_of(Hash)
    end

    it 'works when disabling multiple algorithms' do
      options = GRPC::Core::CompressionOptions.new(
        default_algorithm: :identity,
        default_level: :none,
        disabled_algorithms: [:gzip, :deflate]
      )

      [:gzip, :deflate].each do |algorithm|
        expect(options.algorithm_enabled?(algorithm)).to be false
        expect(options.disabled_algorithms.include?(algorithm)).to be true
      end

      expect(options.default_algorithm).to be(:identity)
      expect(options.default_level).to be(:none)
      expect(options.to_hash).to be_instance_of(Hash)
    end

    it 'works when all optional args have been set' do
      options = GRPC::Core::CompressionOptions.new(
        default_algorithm: :gzip,
        default_level: :low,
        disabled_algorithms: [:deflate]
      )

      expect(options.algorithm_enabled?(:deflate)).to be false
      expect(options.algorithm_enabled?(:gzip)).to be true
      expect(options.disabled_algorithms).to eq([:deflate])

      expect(options.default_algorithm).to be(:gzip)
      expect(options.default_level).to be(:low)
      expect(options.to_hash).to be_instance_of(Hash)
    end

    it 'doesnt fail when no algorithms are disabled' do
      options = GRPC::Core::CompressionOptions.new(
        default_algorithm: :identity,
        default_level: :high
      )

      ALGORITHMS.each do |algorithm|
        expect(options.algorithm_enabled?(algorithm)).to be(true)
      end

      expect(options.disabled_algorithms).to be_empty
      expect(options.default_algorithm).to be(:identity)
      expect(options.default_level).to be(:high)
      expect(options.to_hash).to be_instance_of(Hash)
    end
  end

  describe '#new with bad parameters' do
    it 'should fail with more than one parameter' do
      blk = proc { GRPC::Core::CompressionOptions.new(:gzip, :none) }
      expect { blk.call }.to raise_error
    end

    it 'should fail with a non-hash parameter' do
      blk = proc { GRPC::Core::CompressionOptions.new(:gzip) }
      expect { blk.call }.to raise_error
    end
  end

  describe '#default_algorithm' do
    it 'returns nil if unset' do
      options = GRPC::Core::CompressionOptions.new
      expect(options.default_algorithm).to be(nil)
    end
  end

  describe '#default_level' do
    it 'returns nil if unset' do
      options = GRPC::Core::CompressionOptions.new
      expect(options.default_level).to be(nil)
    end
  end

  describe '#disabled_algorithms' do
    it 'returns an empty list if no algorithms were disabled' do
      options = GRPC::Core::CompressionOptions.new
      expect(options.disabled_algorithms).to be_empty
    end
  end

  describe '#algorithm_enabled?' do
    [:none, :any, 'gzip', Object.new, 1].each do |name|
      it "should fail for parameter ${name} of class #{name.class}" do
        options = GRPC::Core::CompressionOptions.new(
          disabled_algorithms: [:gzip])

        blk = proc do
          options.algorithm_enabled?(name)
        end
        expect { blk.call }.to raise_error
      end
    end
  end
end
