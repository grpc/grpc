// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CRONET_IOS_CRONET_C_FOR_GRPC_H_
#define COMPONENTS_CRONET_IOS_CRONET_C_FOR_GRPC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Cronet Engine API. */

/* Opaque object representing Cronet Engine. Created and configured outside
 * of this API to facilitate sharing with other components */
typedef struct cronet_engine { void* obj; } cronet_engine;

void cronet_engine_add_quic_hint(cronet_engine* engine,
                                 const char* host,
                                 int port,
                                 int alternate_port);

/* Cronet Bidirectional Stream API */

/* Opaque object representing Cronet Bidirectional Stream. */
typedef struct cronet_bidirectional_stream {
  void* obj;
  void* annotation;
} cronet_bidirectional_stream;

/* A single request or response header element. */
typedef struct cronet_bidirectional_stream_header {
  const char* key;
  const char* value;
} cronet_bidirectional_stream_header;

/* Array of request or response headers or trailers. */
typedef struct cronet_bidirectional_stream_header_array {
  size_t count;
  size_t capacity;
  cronet_bidirectional_stream_header* headers;
} cronet_bidirectional_stream_header_array;

/* Set of callbacks used to receive callbacks from bidirectional stream. */
typedef struct cronet_bidirectional_stream_callback {
  /* Invoked when request headers are sent. Indicates that stream has initiated
   * the request. Consumer may call cronet_bidirectional_stream_write() to start
   * writing data.
   */
  void (*on_request_headers_sent)(cronet_bidirectional_stream* stream);

  /* Invoked when initial response headers are received.
   * Consumer must call cronet_bidirectional_stream_read() to start reading.
   * Consumer may call cronet_bidirectional_stream_write() to start writing or
   * close the stream. Contents of |headers| is valid for duration of the call.
   */
  void (*on_response_headers_received)(
      cronet_bidirectional_stream* stream,
      const cronet_bidirectional_stream_header_array* headers,
      const char* negotiated_protocol);

  /* Invoked when data is read into the buffer passed to
   * cronet_bidirectional_stream_read(). Only part of the buffer may be
   * populated. To continue reading, call cronet_bidirectional_stream_read().
   * It may be invoked after on_response_trailers_received()}, if there was
   * pending read data before trailers were received.
   *
   * If count is 0, it means the remote side has signaled that it will send no
   * more data; future calls to cronet_bidirectional_stream_read() will result
   * in the on_data_read() callback or on_succeded() callback if
   * cronet_bidirectional_stream_write() was invoked with end_of_stream set to
   * true.
   */
  void (*on_read_completed)(cronet_bidirectional_stream* stream,
                            char* data,
                            int count);

  /**
   * Invoked when all data passed to cronet_bidirectional_stream_write() is
   * sent.
   * To continue writing, call cronet_bidirectional_stream_write().
   */
  void (*on_write_completed)(cronet_bidirectional_stream* stream,
                             const char* data);

  /* Invoked when trailers are received before closing the stream. Only invoked
   * when server sends trailers, which it may not. May be invoked while there is
   * read data remaining in local buffer. Contents of |trailers| is valid for
   * duration of the call.
   */
  void (*on_response_trailers_received)(
      cronet_bidirectional_stream* stream,
      const cronet_bidirectional_stream_header_array* trailers);

  /**
   * Invoked when there is no data to be read or written and the stream is
   * closed successfully remotely and locally. Once invoked, no further callback
   * methods will be invoked.
   */
  void (*on_succeded)(cronet_bidirectional_stream* stream);

  /**
   * Invoked if the stream failed for any reason after
   * cronet_bidirectional_stream_start(). HTTP/2 error codes are
   * mapped to chrome net error codes. Once invoked, no further callback methods
   * will be invoked.
   */
  void (*on_failed)(cronet_bidirectional_stream* stream, int net_error);

  /**
   * Invoked if the stream was canceled via
   * cronet_bidirectional_stream_cancel(). Once invoked, no further callback
   * methods will be invoked.
   */
  void (*on_canceled)(cronet_bidirectional_stream* stream);
} cronet_bidirectional_stream_callback;

/* Create a new stream object that uses |engine| and |callback|. All stream
 * tasks are performed asynchronously on the |engine| network thread. |callback|
 * methods are invoked synchronously on the |engine| network thread, but must
 * not run tasks on the current thread to prevent blocking networking operations
 * and causing exceptions during shutdown. The |annotation| is stored in
 * bidirectional stream for arbitrary use by application.
 *
 * Returned |cronet_bidirectional_stream*| is owned by the caller, and must be
 * destroyed using |cronet_bidirectional_stream_destroy|.
 *
 * Both |calback| and |engine| must remain valid until stream is destroyed.
 */
cronet_bidirectional_stream* cronet_bidirectional_stream_create(
    cronet_engine* engine,
    void* annotation,
    cronet_bidirectional_stream_callback* callback);

/* TBD: The following methods return int. Should it be a custom type? */

/* Destroy stream object. Destroy could be called from any thread, including
 * network thread, but is posted, so |stream| is valid until calling task is
 * complete.
 */
int cronet_bidirectional_stream_destroy(cronet_bidirectional_stream* stream);

/* Start the stream by sending request to |url| using |method| and |headers|. If
 * |end_of_stream| is true, then no data is expected to be written.
 */
int cronet_bidirectional_stream_start(
    cronet_bidirectional_stream* stream,
    const char* url,
    int priority,
    const char* method,
    const cronet_bidirectional_stream_header_array* headers,
    bool end_of_stream);

/* Read response data into |buffer| of |capacity| length. Must only be called at
 * most once in response to each invocation of the
 * on_response_headers_received() and on_read_completed() methods of the
 * cronet_bidirectional_stream_callback.
 * Each call will result in an invocation of one of the callback's
 * on_read_completed  method if data is read, its on_succeeded() method if
 * the stream is closed, or its on_failed() method if there's an error.
 */
int cronet_bidirectional_stream_read(cronet_bidirectional_stream* stream,
                                     char* buffer,
                                     int capacity);

/* Read response data into |buffer| of |capacity| length. Must only be called at
 * most once in response to each invocation of the
 * on_response_headers_received() and on_read_completed() methods of the
 * cronet_bidirectional_stream_callback.
 * Each call will result in an invocation of one of the callback's
 * on_read_completed  method if data is read, its on_succeeded() method if
 * the stream is closed, or its on_failed() method if there's an error.
 */
int cronet_bidirectional_stream_write(cronet_bidirectional_stream* stream,
                                      const char* buffer,
                                      int count,
                                      bool end_of_stream);

/* Cancels the stream. Can be called at any time after
 * cronet_bidirectional_stream_start(). The on_canceled() method of
 * cronet_bidirectional_stream_callback will be invoked when cancelation
 * is complete and no further callback methods will be invoked. If the
 * stream has completed or has not started, calling
 * cronet_bidirectional_stream_cancel() has no effect and on_canceled() will not
 * be  invoked. At most one callback method may be invoked after
 * cronet_bidirectional_stream_cancel() has completed.
 */
int cronet_bidirectional_stream_cancel(cronet_bidirectional_stream* stream);

/* Returns true if the |stream| was successfully started and is now done
 * (succeeded, canceled, or failed).
 * Returns false if the |stream| stream is not yet started or is in progress.
 */
bool cronet_bidirectional_stream_is_done(cronet_bidirectional_stream* stream);

#ifdef __cplusplus
}
#endif

#endif  // COMPONENTS_CRONET_IOS_CRONET_C_FOR_GRPC_H_
