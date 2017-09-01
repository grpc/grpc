#!/usr/bin/env ruby

# Copyright 2016 gRPC authors.
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

# Worker and worker service implementation

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)
$LOAD_PATH.unshift(this_dir) unless $LOAD_PATH.include?(this_dir)

require 'grpc'

# produces a string of null chars (\0 aka pack 'x') of length l.
def nulls(l)
  fail 'requires #{l} to be +ve' if l < 0
  [].pack('x' * l).force_encoding('ascii-8bit')
end

# load the test-only certificates
def load_test_certs
  this_dir = File.expand_path(File.dirname(__FILE__))
  data_dir = File.join(File.dirname(this_dir), 'spec/testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(data_dir, f)).read }
end


# A EnumeratorQueue wraps a Queue yielding the items added to it via each_item.
class EnumeratorQueue
  extend Forwardable
  def_delegators :@q, :push

  def initialize(sentinel)
    @q = Queue.new
    @sentinel = sentinel
  end

  def each_item
    return enum_for(:each_item) unless block_given?
    loop do
      r = @q.pop
      break if r.equal?(@sentinel)
      fail r if r.is_a? Exception
      yield r
    end
  end
end

# A PingPongEnumerator reads requests and responds one-by-one when enumerated
# via #each_item
class PingPongEnumerator
  def initialize(reqs)
    @reqs = reqs
  end

  def each_item
    return enum_for(:each_item) unless block_given?
    sr = Grpc::Testing::SimpleResponse
    pl = Grpc::Testing::Payload
    @reqs.each do |req|
      yield sr.new(payload: pl.new(body: nulls(req.response_size)))
    end
  end
end
