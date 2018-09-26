# gRPC Endpoints (`grpc_endpoint`) - TCP endpoint
_Author: Sree Kuchibhotla (@sreecha) - Sep 2018_

This document talks about `grpc_endpoint` abstraction and talks about the TCP endpoint's posix implementation in detail.

### What is an endpoint ?
`grpc_endpoint` is an abstraction used by gRPC to read/write bytes. There are different endpoint implementations depending on the underlying transport.


### Endpoint interface

The interface is documented well here: [`endpoint.h`](https://github.com/grpc/grpc/blob/v1.15.1/src/core/lib/iomgr/endpoint.h)

### TCP endpoint

TCP endpoint is implemented here: [tcp_posix.cc](https://github.com/grpc/grpc/blob/v1.15.1/src/core/lib/iomgr/tcp_posix.cc)

#### Read

``` C++
// Implementation of grpc_endpoint_read() API
void tcp_read(grpc_endpoint* ep, grpc_slice_buffer* slices, grpc_closure* cb) {
 // 1. fd = Get grpc_fd from endpoint
 // 2. Store read completion callback “cb” on the endpoint
 // 3. Call polling engine API grpc_fd_notify_on_read(fd, tcp_handle_read)
}
```

``` C++
void tcp_handle_read() {
  // 1. Do the actual reading by calling recvmsg()
  // 2. Call read-completion callback if bytes are read
  // 3. Call grpc_fd_notify_on_read(fd, tcp_handle_read) again if
  //    there are more bytes to read (i.e recvmsg returns EAGAIN)

}
```

#### Write

``` C++
// Implementation of grpc_endpoint_write() API
void tcp_write(grpc_endpoint* ep, grpc_slice_buffer* slices, grpc_closure* cb, void* arg) {
  // 1. fd = Get grpc_fd from endpoint
  // 2. Store write completion callback “cb” on the endpoint
  // 3. Do the actual write (via a helper function tcp_flush) by calling TCP sendmsg()
  // 4. If sendmsg() returned EAGAIN, call notify_on_write() (see below)
}

void notify_on_write() {
  // 1. “Cover” the endpoint by starting adding this endpoint to a backup poller
  //    Note: This is NOT the same backup poller as the one used to maintain
  //    long-lived/infrequently used channels).

  // 2. Call grpc_fd_notify_on_write(fd, tcp_drop_uncovered_and_handle_write) to
  //    register a callback to do the actual write
}

```

##### What does "Covering a write" mean? - and "endpoint backup poller"
gRPC API considers a Write operation to be done the moment it clears ‘flow control’ i.e., and not necessarily sent on the wire.

This means that the application MAY NOT call `grpc_completion_queue_next/pluck` in a timely manner when its `Write()` API is acked. So when a tcp `sendmsg()` returns EAGAIN, the TCP write endpoint needs to make sure that some thread is monitoring the fd-writable event and does the actual writing on the wire. This is the reason why it ‘covers’ the write by adding the endpoint fd to the backup poller

- *What is a backup poller here?:* It is a global `grpc_pollset`. The backup poller runs on the ‘executor’ thread which pretty much calls `grpc_pollset_work()` on the pollset to make progress on the endpoint fds added to it.

