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

Thread.abort_on_exception = true

describe GRPC::Pool do
  Pool = GRPC::Pool

  describe '#new' do
    it 'raises if a non-positive size is used' do
      expect { Pool.new(0) }.to raise_error
      expect { Pool.new(-1) }.to raise_error
      expect { Pool.new(Object.new) }.to raise_error
    end

    it 'is constructed OK with a positive size' do
      expect { Pool.new(1) }.not_to raise_error
    end
  end

  describe '#ready_for_work?' do
    it 'before start it is not ready' do
      p = Pool.new(1)
      expect(p.ready_for_work?).to be(false)
    end

    it 'it stops being ready after all workers jobs waiting or running' do
      p = Pool.new(5)
      p.start
      job = proc { sleep(3) } # sleep so workers busy when done scheduling
      5.times do
        expect(p.ready_for_work?).to be(true)
        p.schedule(&job)
      end
      expect(p.ready_for_work?).to be(false)
    end

    it 'it becomes ready again after jobs complete' do
      p = Pool.new(5)
      p.start
      job = proc {}
      5.times do
        expect(p.ready_for_work?).to be(true)
        p.schedule(&job)
      end
      expect(p.ready_for_work?).to be(false)
      sleep 5 # give the pool time do get at least one task done
      expect(p.ready_for_work?).to be(true)
    end
  end

  describe '#schedule' do
    it 'return if the pool is already stopped' do
      p = Pool.new(1)
      p.stop
      job = proc {}
      expect { p.schedule(&job) }.to_not raise_error
    end

    it 'adds jobs that get run by the pool' do
      p = Pool.new(1)
      p.start
      o, q = Object.new, Queue.new
      job = proc { q.push(o) }
      p.schedule(&job)
      expect(q.pop).to be(o)
      p.stop
    end
  end

  describe '#stop' do
    it 'works when there are no scheduled tasks' do
      p = Pool.new(1)
      expect { p.stop }.not_to raise_error
    end

    it 'stops jobs when there are long running jobs' do
      p = Pool.new(1)
      p.start
      o, q = Object.new, Queue.new
      job = proc do
        sleep(5)  # long running
        q.push(o)
      end
      p.schedule(&job)
      sleep(1)  # should ensure the long job gets scheduled
      expect { p.stop }.not_to raise_error
    end
  end

  describe '#start' do
    it 'runs jobs as they are scheduled' do
      p = Pool.new(5)
      o, q = Object.new, Queue.new
      p.start
      n = 5  # arbitrary
      n.times do
        p.schedule(o, &q.method(:push))
        expect(q.pop).to be(o)
      end
      p.stop
    end
  end
end
