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
        default_algorithm: 'identity',
        default_level: 'none',
        disabled_algorithms: [:gzip, :deflate]
      )

      expect(options.to_hash).to(
        eql('grpc.default_compression_algorithm' => 0,
            'grpc.default_compression_level' => 0,
            'grpc.compression_enabled_algorithms_bitset' => 0x1)
      )
    end

    it 'gives correct channel args with all args set' do
      options = GRPC::Core::CompressionOptions.new(
        default_algorithm: 'gzip',
        default_level: 'low',
        disabled_algorithms: ['deflate']
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
        default_algorithm: 'identity',
        default_level: 'high'
      )

      expect(options.to_hash).to(
        eql('grpc.default_compression_algorithm' => ALGORITHMS[:identity],
            'grpc.default_compression_level' => COMPRESS_LEVELS[:high],
            'grpc.compression_enabled_algorithms_bitset' => ALL_ENABLED_BITSET)
      )
    end

    # Raising an error in when attempting to set the default algorithm
    # to something that is also requested to be disabled
    it 'gives raises an error when the chosen default algorithm is disabled' do
      blk = proc do
        GRPC::Core::CompressionOptions.new(
          default_algorithm: :gzip,
          disabled_algorithms: [:gzip])
      end
      expect { blk.call }.to raise_error
    end
  end

  # Test the private methods in the C extension that interact with
  # the wrapped grpc_compression_options.
  #
  # Using #send to call private methods.
  describe 'private internal methods' do
    it 'mutating functions and accessors should be private' do
      options = GRPC::Core::CompressionOptions.new

      [:disable_algorithm_internal,
       :disable_algorithms,
       :set_default_algorithm,
       :set_default_level,
       :default_algorithm_internal_value,
       :default_level_internal_value].each do |method_name|
        expect(options.private_methods).to include(method_name)
      end
    end

    describe '#disable_algorithms' do
      ALGORITHMS.each_pair do |name, internal_value|
        it "passes #{internal_value} to internal method for #{name}" do
          options = GRPC::Core::CompressionOptions.new
          expect(options).to receive(:disable_algorithm_internal)
            .with(internal_value)

          options.send(:disable_algorithms, name)
        end
      end

      it 'should work with multiple parameters' do
        options = GRPC::Core::CompressionOptions.new

        ALGORITHMS.values do |internal_value|
          expect(options).to receive(:disable_algorithm_internal)
            .with(internal_value)
        end

        # disabled_algorithms is a private, variadic method
        options.send(:disable_algorithms, *ALGORITHMS.keys)
      end
    end

    describe '#new default values' do
      it 'should start out with all algorithms enabled' do
        options = GRPC::Core::CompressionOptions.new
        bitset = options.send(:enabled_algorithms_bitset)
        expect(bitset).to eql(ALL_ENABLED_BITSET)
      end

      it 'should start out with no default algorithm' do
        options = GRPC::Core::CompressionOptions.new
        expect(options.send(:default_algorithm_internal_value)).to be_nil
      end

      it 'should start out with no default level' do
        options = GRPC::Core::CompressionOptions.new
        expect(options.send(:default_level_internal_value)).to be_nil
      end
    end

    describe '#enabled_algoritms_bitset' do
      it 'should respond to disabling one algorithm' do
        options = GRPC::Core::CompressionOptions.new
        options.send(:disable_algorithms, :gzip)
        current_bitset = options.send(:enabled_algorithms_bitset)
        expect(current_bitset & ALGORITHM_BITS[:gzip]).to be_zero
      end

      it 'should respond to disabling multiple algorithms' do
        options = GRPC::Core::CompressionOptions.new

        # splitting up algorithms array since #disable_algorithms is variadic
        options.send(:disable_algorithms, *ALGORITHMS.keys)
        current_bitset = options.send(:enabled_algorithms_bitset)
        expect(current_bitset).to eql(ALL_DISABLED_BITSET)
      end
    end

    describe 'setting the default algorithm by name' do
      it 'should set the internal value of the default algorithm' do
        ALGORITHMS.each_pair do |name, expected_internal_value|
          options = GRPC::Core::CompressionOptions.new
          options.send(:set_default_algorithm, name)
          internal_value = options.send(:default_algorithm_internal_value)
          expect(internal_value).to eql(expected_internal_value)
        end
      end

      it 'should fail with invalid algorithm names' do
        [:none, :low, :huffman, :unkown, Object.new, 1].each do |name|
          blk = proc do
            options = GRPC::CoreCompressionOptions.new
            options.send(:set_default_algorithm, name)
          end
          expect { blk.call }.to raise_error
        end
      end
    end

    describe 'setting the default level by name' do
      it 'should set the internal value of the default compression value' do
        COMPRESS_LEVELS.each_pair do |level, expected_internal_value|
          options = GRPC::Core::CompressionOptions.new
          options.send(:set_default_level, level)
          internal_value = options.send(:default_level_internal_value)
          expect(internal_value).to eql(expected_internal_value)
        end
      end

      it 'should fail with invalid names' do
        [:identity, :gzip, :unkown, :any, Object.new, 1].each do |name|
          blk = proc do
            GRPC::Core::CompressionOptions.new.send(:set_default_level, name)
          end
          expect { blk.call }.to raise_error
        end
      end
    end
  end
end
