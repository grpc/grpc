/*
 * Copyright 2014 The gRPC Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package io.grpc.testing.integration

import com.google.protobuf.ByteString
import io.grpc.ForwardingServerCall
import io.grpc.Metadata
import io.grpc.ServerCall
import io.grpc.ServerCallHandler
import io.grpc.ServerInterceptor
import io.grpc.Status
import java.util.Random
import java.util.concurrent.Executor
import java.util.concurrent.TimeUnit
import kotlin.math.min
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flatMapConcat
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.fold
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.toList

/**
 * Implementation of the business logic for the TestService. Uses an executor to schedule chunks
 * sent in response streams.
 */
@ExperimentalCoroutinesApi
@FlowPreview // most of these methods are graduating imminently but that has not yet landed
class TestServiceImpl(
  executor: Executor
) : TestServiceGrpcKt.TestServiceCoroutineImplBase(executor.asCoroutineDispatcher()) {
  private val random = Random()
  private val compressableBuffer: ByteString = ByteString.copyFrom(ByteArray(1024))

  override suspend fun emptyCall(request: EmptyProtos.Empty): EmptyProtos.Empty =
    EmptyProtos.Empty.getDefaultInstance()

  override suspend fun unaryCall(request: Messages.SimpleRequest): Messages.SimpleResponse {
    if (request.hasResponseStatus()) {
      throw Status
        .fromCodeValue(request.responseStatus.code)
        .withDescription(request.responseStatus.message)
        .asException()
    }
    return Messages.SimpleResponse
      .newBuilder()
      .apply {
        if (request.responseSize != 0) {
          val offset = random.nextInt(compressableBuffer.size())
          payload = generatePayload(compressableBuffer, offset, request.responseSize)
        }
      }
      .build()
  }

  override fun streamingOutputCall(
    request: Messages.StreamingOutputCallRequest
  ): Flow<Messages.StreamingOutputCallResponse> {
    return flow {
      var offset = 0
      for (params in request.responseParametersList) {
        delay(timeMillis = TimeUnit.MICROSECONDS.toMillis(params.intervalUs.toLong()))
        emit(
          Messages.StreamingOutputCallResponse
            .newBuilder()
            .apply {
              payload = generatePayload(compressableBuffer, offset, params.size)
            }
            .build()
        )
        offset += params.size
        offset %= compressableBuffer.size()
      }
    }
  }

  override suspend fun streamingInputCall(
    requests: Flow<Messages.StreamingInputCallRequest>
  ): Messages.StreamingInputCallResponse =
    Messages.StreamingInputCallResponse
      .newBuilder()
      .apply {
        aggregatedPayloadSize = requests.map { it.payload.body.size() }.sum()
      }
      .build()

  override fun fullDuplexCall(
    requests: Flow<Messages.StreamingOutputCallRequest>
  ): Flow<Messages.StreamingOutputCallResponse> =
    requests.flatMapConcat {
      if (it.hasResponseStatus()) {
        throw Status
          .fromCodeValue(it.responseStatus.code)
          .withDescription(it.responseStatus.message)
          .asException()
      }
      streamingOutputCall(it)
    }

  override fun halfDuplexCall(
    requests: Flow<Messages.StreamingOutputCallRequest>
  ): Flow<Messages.StreamingOutputCallResponse> =
    flow {
      val requestList = requests.toList()
      emitAll(requestList.asFlow().flatMapConcat { streamingOutputCall(it) })
    }

  companion object {
    /** Returns interceptors necessary for full service implementation.  */
    @get:JvmStatic
    @get:JvmName("interceptors")
    val interceptors = listOf(
      echoRequestHeadersInterceptor(Util.METADATA_KEY),
      echoRequestMetadataInHeaders(Util.ECHO_INITIAL_METADATA_KEY),
      echoRequestMetadataInTrailers(Util.ECHO_TRAILING_METADATA_KEY)
    )

    suspend fun Flow<Int>.sum() = fold(0) { a, b -> a + b }

    /**
     * Generates a payload of desired type and size. Reads compressableBuffer or
     * uncompressableBuffer as a circular buffer.
     */
    private fun generatePayload(dataBuffer: ByteString, offset: Int, size: Int): Messages.Payload {
      val payloadChunks = mutableListOf<ByteString>()
      // This offset would never pass the array boundary.
      var begin = offset
      var end: Int
      var bytesLeft = size
      while (bytesLeft > 0) {
        end = min(begin + bytesLeft, dataBuffer.size())
        // ByteString.substring returns the substring from begin, inclusive, to end, exclusive.
        payloadChunks += dataBuffer.substring(begin, end)
        bytesLeft -= end - begin
        begin = end % dataBuffer.size()
      }
      return Messages.Payload.newBuilder().setBody(ByteString.copyFrom(payloadChunks)).build()
    }

    /**
     * Echo the request headers from a client into response headers and trailers. Useful for
     * testing end-to-end metadata propagation.
     */
    private fun echoRequestHeadersInterceptor(vararg keys: Metadata.Key<*>): ServerInterceptor {
      val keySet: Set<Metadata.Key<*>> = keys.toSet()
      return object : ServerInterceptor {
        override fun <ReqT, RespT> interceptCall(
          call: ServerCall<ReqT, RespT>,
          requestHeaders: Metadata,
          next: ServerCallHandler<ReqT, RespT>
        ): ServerCall.Listener<ReqT> =
          next.startCall(
            object : ForwardingServerCall.SimpleForwardingServerCall<ReqT, RespT>(call) {
              override fun sendHeaders(responseHeaders: Metadata) {
                responseHeaders.merge(requestHeaders, keySet)
                super.sendHeaders(responseHeaders)
              }

              override fun close(status: Status, trailers: Metadata) {
                trailers.merge(requestHeaders, keySet)
                super.close(status, trailers)
              }
            },
            requestHeaders
          )
      }
    }

    /**
     * Echoes request headers with the specified key(s) from a client into response headers only.
     */
    private fun echoRequestMetadataInHeaders(vararg keys: Metadata.Key<*>): ServerInterceptor {
      val keySet: Set<Metadata.Key<*>> = keys.toSet()
      return object : ServerInterceptor {
        override fun <ReqT, RespT> interceptCall(
          call: ServerCall<ReqT, RespT>,
          requestHeaders: Metadata,
          next: ServerCallHandler<ReqT, RespT>
        ): ServerCall.Listener<ReqT> =
          next.startCall(
            object : ForwardingServerCall.SimpleForwardingServerCall<ReqT, RespT>(call) {
              override fun sendHeaders(responseHeaders: Metadata) {
                responseHeaders.merge(requestHeaders, keySet)
                super.sendHeaders(responseHeaders)
              }
            },
            requestHeaders
          )
      }
    }

    /**
     * Echoes request headers with the specified key(s) from a client into response trailers only.
     */
    private fun echoRequestMetadataInTrailers(vararg keys: Metadata.Key<*>): ServerInterceptor {
      val keySet: Set<Metadata.Key<*>> = keys.toSet()
      return object : ServerInterceptor {
        override fun <ReqT, RespT> interceptCall(
          call: ServerCall<ReqT, RespT>,
          requestHeaders: Metadata,
          next: ServerCallHandler<ReqT, RespT>
        ): ServerCall.Listener<ReqT> =
          next.startCall(
            object : ForwardingServerCall.SimpleForwardingServerCall<ReqT, RespT>(call) {
              override fun close(status: Status, trailers: Metadata) {
                trailers.merge(requestHeaders, keySet)
                super.close(status, trailers)
              }
            },
            requestHeaders
          )
      }
    }
  }
}
