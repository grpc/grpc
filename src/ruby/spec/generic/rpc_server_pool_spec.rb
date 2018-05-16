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

    it 'it stops being ready after all workers are busy' do
      p = Pool.new(5)
      p.start

      wait_mu = Mutex.new
      wait_cv = ConditionVariable.new
      wait = true

      job = proc do
        wait_mu.synchronize do
          wait_cv.wait(wait_mu) while wait
        end
      end

      5.times do
        expect(p.ready_for_work?).to be(true)
        p.schedule(&job)
      end

      expect(p.ready_for_work?).to be(false)

      wait_mu.synchronize do
        wait = false
        wait_cv.broadcast
      end
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

      wait_forever_mu = Mutex.new
      wait_forever_cv = ConditionVariable.new
      wait_forever = true

      job_running = Queue.new
      job = proc do
        job_running.push(Object.new)
        wait_forever_mu.synchronize do
          wait_forever_cv.wait while wait_forever
        end
      end
      p.schedule(&job)
      job_running.pop
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
