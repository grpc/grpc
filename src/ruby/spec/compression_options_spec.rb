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
  # Names of supported compression algorithms and their internal enum values
  ALGORITHMS = {
    identity: 0,
    deflate: 1,
    gzip: 2
  }

  # Compression algorithms and their corresponding bits in the internal
  # enabled algorithms bitset for GRPC core channel args.
  ALGORITHM_BITS = {
    identity: 0x1,
    deflate: 0x2,
    gzip: 0x4
  }

  # "enabled algorithms bitset" when all compression algorithms are enabled
  ALL_ENABLED_BITSET = 0x7

  # "enabled algorithms bitset" when all compression algorithms are disabled
  ALL_DISABLED_BITSET = 0x0

  # Names of valid supported compression levels and their internal enum values
  COMPRESS_LEVELS = {
    none: 0,
    low: 1,
    medium: 2,
    high: 3
  }

  it 'compression level name constants should match expections' do
    expect(GRPC::Core::CompressionOptions::COMPRESS_NONE_SYM).to eq(:none)
    expect(GRPC::Core::CompressionOptions::COMPRESS_LOW_SYM).to eq(:low)
    expect(GRPC::Core::CompressionOptions::COMPRESS_MEDIUM_SYM).to eq(:medium)
    expect(GRPC::Core::CompressionOptions::COMPRESS_HIGH_SYM).to eq(:high)
  end

  it 'implements to_s' do
    expect { GRPC::Core::CompressionOptions.new.to_s }.to_not raise_error
  end

  it '#to_channel_arg_hash gives the same result as #to_hash' do
    options = GRPC::Core::CompressionOptions.new
    expect(options.to_channel_arg_hash).to eql(options.to_hash)
  end

  # Test the normal call sequence of creating an instance
  # and then obtaining the resulting channel-arg hash that
  # corresponds to the compression settings of the instance
  describe 'creating and converting to channel args hash' do
    it 'gives the correct channel args when nothing has been adjusted yet' do
      expect(GRPC::Core::CompressionOptions.new.to_hash).to(
        eql('grpc.compression_enabled_algorithms_bitset' => 0x7))
    end

    it 'gives the correct channel args after everything has been disabled' do
      options = GRPC::Core::CompressionOptions.new(
        default_algorithm: :identity,
        default_level: :none,
        disabled_algorithms: ALGORITHMS.keys
      )

      channel_arg_hash = options.to_hash
      expect(channel_arg_hash['grpc.default_compression_algorithm']).to eq(0)
      expect(channel_arg_hash['grpc.default_compression_level']).to eq(0)

      # Don't care if the "identity" algorithm bit is set or unset
      bitset = channel_arg_hash['grpc.compression_enabled_algorithms_bitset']
      expect(bitset & ~ALGORITHM_BITS[:identity]).to eq(0)
    end

    it 'gives correct channel args with all args set' do
      options = GRPC::Core::CompressionOptions.new(
        default_algorithm: :gzip,
        default_level: :low,
        disabled_algorithms: [:deflate]
      )

      expected_bitset = ALL_ENABLED_BITSET & ~ALGORITHM_BITS[:deflate]

      expect(options.to_hash).to(
        eql('grpc.default_compression_algorithm' => ALGORITHMS[:gzip],
            'grpc.default_compression_level' => COMPRESS_LEVELS[:low],
            'grpc.compression_enabled_algorithms_bitset' => expected_bitset)
      )
    end

    it 'gives correct channel args when no algorithms are disabled' do
      options = GRPC::Core::CompressionOptions.new(
        default_algorithm: :identity,
        default_level: :high
      )

      expect(options.to_hash).to(
        eql('grpc.default_compression_algorithm' => ALGORITHMS[:identity],
            'grpc.default_compression_level' => COMPRESS_LEVELS[:high],
            'grpc.compression_enabled_algorithms_bitset' => ALL_ENABLED_BITSET)
      )
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

  describe '#level_name_to_value' do
    it 'should give the correct internal values from compression level names' do
      cls = GRPC::Core::CompressionOptions
      COMPRESS_LEVELS.each_pair do |name, internal_value|
        expect(cls.level_name_to_value(name)).to eq(internal_value)
      end
    end

    [:gzip, :deflate, :any, Object.new, 'none', 'low', 1].each do |name|
      it "should fail for parameter #{name} of class #{name.class}" do
        blk = proc do
          GRPC::Core::CompressionOptions.level_name_to_value(name)
        end
        expect { blk.call }.to raise_error
      end
    end
  end

  describe '#level_value_to_name' do
    it 'should give the correct internal values from compression level names' do
      cls = GRPC::Core::CompressionOptions
      COMPRESS_LEVELS.each_pair do |name, internal_value|
        expect(cls.level_value_to_name(internal_value)).to eq(name)
      end
    end

    [:gzip, :any, Object.new, '1', :low].each do |name|
      it "should fail for parameter #{name} of class #{name.class}" do
        blk = proc do
          GRPC::Core::CompressionOptions.level_value_to_name(name)
        end
        expect { blk.call }.to raise_error
      end
    end
  end

  describe '#algorithm_name_to_value' do
    it 'should give the correct internal values from algorithm names' do
      cls = GRPC::Core::CompressionOptions
      ALGORITHMS.each_pair do |name, internal_value|
        expect(cls.algorithm_name_to_value(name)).to eq(internal_value)
      end
    end

    ['gzip', 'deflate', :any, Object.new, :none, :low, 1].each do |name|
      it "should fail for parameter #{name} of class #{name.class}" do
        blk = proc do
          GRPC::Core::CompressionOptions.algorithm_name_to_value(name)
        end
        expect { blk.call }.to raise_error
      end
    end
  end

  describe '#algorithm_value_to_name' do
    it 'should give the correct internal values from algorithm names' do
      cls = GRPC::Core::CompressionOptions
      ALGORITHMS.each_pair do |name, internal_value|
        expect(cls.algorithm_value_to_name(internal_value)).to eq(name)
      end
    end

    ['gzip', :deflate, :any, Object.new, :low, '1'].each do |value|
      it "should fail for parameter #{value} of class #{value.class}" do
        blk = proc do
          GRPC::Core::CompressionOptions.algorithm_value_to_name(value)
        end
        expect { blk.call }.to raise_error
      end
    end
  end

  describe '#default_algorithm and #default_algorithm_internal_value' do
    it 'can set the default algorithm and then read it back out' do
      ALGORITHMS.each_pair do |name, internal_value|
        options = GRPC::Core::CompressionOptions.new(default_algorithm: name)
        expect(options.default_algorithm).to eq(name)
        expect(options.default_algorithm_internal_value).to eq(internal_value)
      end
    end

    it 'returns nil if unset' do
      options = GRPC::Core::CompressionOptions.new
      expect(options.default_algorithm).to be_nil
      expect(options.default_algorithm_internal_value).to be_nil
    end
  end

  describe '#default_level and #default_level_internal_value' do
    it 'can set the default level and read it back out' do
      COMPRESS_LEVELS.each_pair do |name, internal_value|
        options = GRPC::Core::CompressionOptions.new(default_level: name)
        expect(options.default_level).to eq(name)
        expect(options.default_level_internal_value).to eq(internal_value)
      end
    end

    it 'returns nil if unset' do
      options = GRPC::Core::CompressionOptions.new
      expect(options.default_level).to be_nil
      expect(options.default_level_internal_value).to be_nil
    end
  end

  describe '#disabled_algorithms' do
    it 'can set the disabled algorithms and read them back out' do
      options = GRPC::Core::CompressionOptions.new(
        disabled_algorithms: [:gzip, :deflate])

      [:gzip, :deflate].each do |name|
        expect(options.disabled_algorithms.include?(name)).to eq(true)
      end
      expect(options.disabled_algorithms.size).to eq(2)
    end

    it 'returns an empty list if no algorithms were disabled' do
      options = GRPC::Core::CompressionOptions.new
      expect(options.disabled_algorithms).to eq([])
    end
  end

  describe '#is_algorithm_enabled' do
    it 'returns true if the algorithm is valid and not disabled' do
      options = GRPC::Core::CompressionOptions.new(disabled_algorithms: [:gzip])
      expect(options.is_algorithm_enabled(:deflate)).to eq(true)
    end

    it 'returns false if the algorithm is valid and disabled' do
      options = GRPC::Core::CompressionOptions.new(disabled_algorithms: [:gzip])
      expect(options.is_algorithm_enabled(:gzip)).to eq(false)
    end

    [:none, :any, 'gzip', Object.new, 1].each do |name|
      it "should fail for parameter ${name} of class #{name.class}" do
        options = GRPC::Core::CompressionOptions.new(
          disabled_algorithms: [:gzip])

        blk = proc do
          options.is_algorithm_enabled(name)
        end
        expect { blk.call }.to raise_error
      end
    end
  end

  describe '#enabled_algoritms_bitset' do
    it 'should respond to not disabling any algorithms' do
      options = GRPC::Core::CompressionOptions.new
      expect(options.enabled_algorithms_bitset).to eq(ALL_ENABLED_BITSET)
    end

    it 'should respond to disabling one algorithm' do
      options = GRPC::Core::CompressionOptions.new(
        disabled_algorithms: [:gzip])
      expect(options.enabled_algorithms_bitset & ALGORITHM_BITS[:gzip]).to eq(0)
    end

    it 'should respond to disabling multiple algorithms' do
      options = GRPC::Core::CompressionOptions.new(
        disabled_algorithms: ALGORITHMS.keys)
      expect(options.enabled_algorithms_bitset).to eql(ALL_DISABLED_BITSET)
    end
  end
end
