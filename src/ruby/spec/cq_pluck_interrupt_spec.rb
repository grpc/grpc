# Copyright 2026 gRPC authors.
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

require 'spec_helper'

# Regression test for https://github.com/grpc/grpc/issues/42541.
#
# A Ruby exception raised in a thread that is blocked in
# rb_completion_queue_pluck (run_batch) unwinds past the C frames, and the
# rb_ensure frees the in-flight run_batch_stack while the batch is still
# pending. When the batch then completes, the core writes the received
# metadata/message/status into that freed stack — a use-after-free that
# corrupts the heap (valgrind: "Invalid write ... inside a block of size 840
# free'd ... by rb_ensure") and can crash the VM. The queue is also left with
# an event nobody will ever drain.
#
# The scenario runs in a child process: the defect corrupts memory, and the
# spec must report that instead of dying with the runner. The child is
# timeout-bounded so a VM hang shows up as a failure rather than wedging the
# suite. The child detects the corruption with same-size-class canary
# allocations on the interrupted thread (see the support script).
describe 'GRPC::Core::Call#run_batch thread interrupt (issue #42541)' do
  def run_repro(mode)
    repro = File.expand_path('support/cq_pluck_interrupt_repro.rb', __dir__)
    lib_dir = File.expand_path('../lib', __dir__)
    out_r, out_w = IO.pipe
    pid = Process.spawn(RbConfig.ruby, "-I#{lib_dir}", repro, mode,
                        out: out_w, err: out_w)
    out_w.close
    deadline = Time.now + 60
    status = nil
    loop do
      pid_done, status = Process.waitpid2(pid, Process::WNOHANG)
      break status unless pid_done.nil?
      if Time.now > deadline
        Process.kill('KILL', pid)
        Process.waitpid(pid)
        raise "repro #{mode} did not finish within 60s (VM hang)"
      end
      sleep 0.1
    end
    output = out_r.read
    out_r.close
    [status, output]
  end

  it 'exits cleanly when the blocked batch is interrupted and the call is GCed' do
    status, output = run_repro('crash')
    expect(status.success?).to be(true), lambda {
      "interrupted run_batch corrupted memory / crashed the VM " \
        "(status #{status.inspect}):\n#{output}"
    }
    expect(output).to include('[repro] CANARY:INTACT')
    expect(output).to include('[repro] SURVIVED')
  end

  it 'exits cleanly without the interrupt (control)' do
    status, output = run_repro('control')
    expect(status.success?).to be(true), lambda {
      "control run_batch failed (status #{status.inspect}):\n#{output}"
    }
    expect(output).to include('[repro] CANARY:INTACT')
    expect(output).to include('[repro] SURVIVED')
  end
end
