# C++ Performance Notes

## Streaming write buffering

Generally, each write operation (Write(), WritesDone()) implies a syscall.
gRPC will try to batch together separate write operations from different
threads, but currently cannot automatically infer batching in a single stream.

If message k+1 in a stream does not rely on responses from message k, it's
possible to enable write batching by passing a WriteOptions argument to Write
with the buffer_hint set:

~~~{.cpp}
stream_writer->Write(message, WriteOptions().set_buffer_hint());
~~~

The write will be buffered until one of the following is true:
- the per-stream buffer is filled (controllable with the channel argument
  GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE) - this prevents infinite buffering leading
  to OOM
- a subsequent Write without buffer_hint set is posted
- the call is finished for writing (WritesDone() called on the client,
  or Finish() called on an async server stream, or the service handler returns
  for a sync server stream)

## Completion Queues and Threading in the Async API

Right now, the best performance trade-off is having numcpu's threads and one
completion queue per thread.
