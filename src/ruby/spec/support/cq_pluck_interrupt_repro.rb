# Standalone scenario for the run_batch thread-interrupt defect (issue #42541),
# run in a child process by cq_pluck_interrupt_spec.rb because the defect
# corrupts memory.
#
# Mechanism: a Ruby exception interrupts a thread blocked in
# rb_completion_queue_pluck (run_batch). The rb_ensure frees the in-flight
# run_batch_stack (840 bytes: ops + recv result arrays) while the batch is
# still pending. When the batch then completes (a real server answers), the
# core writes the received metadata/message/status into that freed block — a
# use-after-free write (valgrind: "Invalid write ... 756 bytes inside a block
# of size 840 free'd ... by rb_ensure").
#
# The oracle: the interrupted thread's Ruby-level ensure allocates canary
# strings in the same malloc size class as the run_batch_stack (glibc tcache
# is per-thread and LIFO, so the first 840-byte allocation on this thread
# takes the chunk the C-level ensure just freed). When the core's completion
# writes into it, the canary is corrupted and the scenario reports it. Without
# the interrupt (control), or with the fix (which deliberately leaks the stack
# instead of freeing it while the batch is in flight), the canaries stay
# intact.
#
# Usage: ruby cq_pluck_interrupt_repro.rb [crash|control]

mode = ARGV[0] || 'crash'
require 'grpc'

CANARY_LEN = 832 # buffer request len+1=833 -> same glibc 848-chunk bin as gpr_malloc(840)

server = GRPC::Core::Server.new(nil)
port = server.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
server.start
rpc_q = Queue.new
respond_q = Queue.new
Thread.new do
  begin
    rpc = server.request_call
    rpc_q.push(rpc)
    respond_q.pop # wait until the client thread has been interrupted (or not)
    rpc.call.run_batch(GRPC::Core::CallOps::SEND_INITIAL_METADATA => {})
    rpc.call.run_batch(GRPC::Core::CallOps::SEND_MESSAGE => 'PONG')
    rpc.call.run_batch(
      GRPC::Core::CallOps::SEND_STATUS_FROM_SERVER => Struct::Status.new(0, 'OK')
    )
  rescue Exception
    nil
  end
end

ch = GRPC::Core::Channel.new("127.0.0.1:#{port}", nil, :this_channel_is_insecure)
call = ch.create_call(nil, nil, '/repro/Method', nil, Time.now + 30)

canaries = nil
t = Thread.new do
  begin
    call.run_batch(
      GRPC::Core::CallOps::SEND_INITIAL_METADATA => {},
      GRPC::Core::CallOps::RECV_INITIAL_METADATA => true,
      GRPC::Core::CallOps::RECV_MESSAGE => nil,
      GRPC::Core::CallOps::RECV_STATUS_ON_CLIENT => nil
    )
  rescue Exception
    nil
  ensure
    # Allocated on this thread, immediately after the C-level rb_ensure has
    # freed the in-flight run_batch_stack (same thread's tcache, LIFO).
    canaries = Array.new(8) { 'A' * CANARY_LEN }
  end
end

sleep 1 # client batch is now blocked in rb_completion_queue_pluck

if mode == 'crash'
  t.raise(GRPC::Cancelled) # longjmp out of pluck; ensure frees the in-flight stack
  unless t.join(10) # wait for the unwind: stack freed, canaries allocated
    STDOUT.puts '[repro] THREAD DID NOT FINISH within 10s (VM hang)'
    exit 3
  end
end

respond_q.push(:go) # let the server complete the client's batch

if mode != 'crash'
  unless t.join(10) # control: run_batch returns once the server has answered
    STDOUT.puts '[repro] THREAD DID NOT FINISH within 10s (VM hang)'
    exit 3
  end
end

# The completion delivery needs exec-ctx pumps (a dead thread's pluck is not
# there to pump anymore). Pump via channel ops until the write lands or the
# budget runs out.
corrupted = []
50.times do
  ch.connectivity_state(true) # runs an exec ctx
  corrupted = canaries.each_index.select do |i|
    canaries[i].count('A') != CANARY_LEN
  end
  break unless corrupted.empty?
  sleep 0.2
end

if corrupted.empty?
  STDOUT.puts '[repro] CANARY:INTACT'
else
  STDOUT.puts "[repro] CANARY:CORRUPTED (#{corrupted.join(',')})"
end

t = nil
call = nil
STDOUT.puts '[repro] SURVIVED'
exit(corrupted.empty? ? 0 : 4)
