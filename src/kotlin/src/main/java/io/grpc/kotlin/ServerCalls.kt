/*
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package io.grpc.kotlin

import io.grpc.Metadata as GrpcMetadata
import io.grpc.MethodDescriptor
import io.grpc.MethodDescriptor.MethodType.BIDI_STREAMING
import io.grpc.MethodDescriptor.MethodType.CLIENT_STREAMING
import io.grpc.MethodDescriptor.MethodType.SERVER_STREAMING
import io.grpc.MethodDescriptor.MethodType.UNARY
import io.grpc.ServerCall
import io.grpc.ServerCallHandler
import io.grpc.ServerMethodDefinition
import io.grpc.Status
import io.grpc.StatusException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineName
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.async
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.plus
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Helpers for implementing a gRPC server based on a Kotlin coroutine implementation.
 */
object ServerCalls {
  /**
   * Creates a [ServerMethodDefinition] that implements the specified unary RPC method by running
   * the specified implementation and associated implementation details within the specified
   * [CoroutineScope] (and/or a subscope).
   *
   * When the RPC is received, this method definition will pass the request from the client
   * to [implementation], and send the response back to the client when it is returned.
   *
   * If [implementation] fails with a [StatusException], the RPC will fail with the corresponding
   * [Status].  If [implementation] fails with a [CancellationException], the RPC will fail
   * with [Status.CANCELLED].  If [implementation] fails for any other reason, the RPC will
   * fail with [Status.UNKNOWN] with the exception as a cause.  If a cancellation is received
   * from the client before [implementation] is complete, the coroutine will be cancelled and the
   * RPC will fail with [Status.CANCELLED].
   *
   * @param scope The scope to run the RPC implementation in
   * @param descriptor The descriptor of the method being implemented
   * @param implementation The implementation of the RPC method
   */
  fun <RequestT, ResponseT> unaryServerMethodDefinition(
    scope: CoroutineScope,
    descriptor: MethodDescriptor<RequestT, ResponseT>,
    implementation: suspend (request: RequestT) -> ResponseT
  ): ServerMethodDefinition<RequestT, ResponseT> {
    require(descriptor.type == UNARY) {
      "Expected a unary method descriptor but got $descriptor"
    }
    return serverMethodDefinition(scope, descriptor) { requests ->
      flow {
        val request = requests.singleOrStatus("request", descriptor)
        val response = implementation(request)
        emit(response)
      }
    }
  }

  /**
   * Creates a [ServerMethodDefinition] that implements the specified client-streaming RPC method by
   * running the specified implementation and associated implementation details within the specified
   * [CoroutineScope] (and/or a subscope).
   *
   * When the RPC is received, this method definition will pass a [Flow] of requests from the client
   * to [implementation], and send the response back to the client when it is returned.
   * Exceptions are handled as in [unaryServerMethodDefinition].  Additionally, attempts to collect
   * the requests flow more than once will throw an [IllegalStateException], and if [implementation]
   * cancels collection of the requests flow, further requests from the client will be ignored
   * (and no backpressure will be applied).
   *
   * @param scope The scope to run the RPC implementation in
   * @param descriptor The descriptor of the method being implemented
   * @param implementation The implementation of the RPC method
   */
  fun <RequestT, ResponseT> clientStreamingServerMethodDefinition(
    scope: CoroutineScope,
    descriptor: MethodDescriptor<RequestT, ResponseT>,
    implementation: suspend (requests: Flow<RequestT>) -> ResponseT
  ): ServerMethodDefinition<RequestT, ResponseT> {
    require(descriptor.type == CLIENT_STREAMING) {
      "Expected a client streaming method descriptor but got $descriptor"
    }
    return serverMethodDefinition(scope, descriptor) { requests ->
      flow {
        val response = implementation(requests)
        emit(response)
      }
    }
  }

  /**
   * Creates a [ServerMethodDefinition] that implements the specified server-streaming RPC method by
   * running the specified implementation and associated implementation details within the specified
   * [CoroutineScope] (and/or a subscope).  When the RPC is received, this method definition will
   * collect the flow returned by [implementation] and send the emitted values back to the client.
   *
   * When the RPC is received, this method definition will pass the request from the client
   * to [implementation], and collect the returned [Flow], sending responses to the client as they
   * are emitted.  Exceptions and cancellation are handled as in [unaryServerMethodDefinition].
   *
   * @param scope The scope to run the RPC implementation in
   * @param descriptor The descriptor of the method being implemented
   * @param implementation The implementation of the RPC method
   */
  fun <RequestT, ResponseT> serverStreamingServerMethodDefinition(
    scope: CoroutineScope,
    descriptor: MethodDescriptor<RequestT, ResponseT>,
    implementation: (request: RequestT) -> Flow<ResponseT>
  ): ServerMethodDefinition<RequestT, ResponseT> {
    require(descriptor.type == SERVER_STREAMING) {
      "Expected a server streaming method descriptor but got $descriptor"
    }
    return serverMethodDefinition(scope, descriptor) {
      requests -> flow {
        val request = requests.singleOrStatus("request", descriptor)
        val responses = implementation(request)
        emitAll(responses)
      }
    }
  }

  /**
   * Creates a [ServerMethodDefinition] that implements the specified bidirectional-streaming RPC
   * method by running the specified implementation and associated implementation details within the
   * specified [CoroutineScope] (and/or a subscope).
   *
   * When the RPC is received, this method definition will pass a [Flow] of requests from the client
   * to [implementation], and collect the returned [Flow], sending responses to the client as they
   * are emitted.
   *
   * Exceptions and cancellation are handled as in [clientStreamingServerMethodDefinition] and as
   * in [serverStreamingServerMethodDefinition].
   *
   * @param scope The scope to run the RPC implementation in
   * @param descriptor The descriptor of the method being implemented
   * @param implementation The implementation of the RPC method
   */
  fun <RequestT, ResponseT> bidiStreamingServerMethodDefinition(
    scope: CoroutineScope,
    descriptor: MethodDescriptor<RequestT, ResponseT>,
    implementation: (requests: Flow<RequestT>) -> Flow<ResponseT>
  ): ServerMethodDefinition<RequestT, ResponseT> {
    require(descriptor.type == BIDI_STREAMING) {
      "Expected a bidi streaming method descriptor but got $descriptor"
    }
    return serverMethodDefinition(scope, descriptor, implementation)
  }

  /**
   * Builds a [ServerMethodDefinition] that implements the specified RPC method by running the
   * specified channel-based implementation within the specified [CoroutineScope] (and/or a
   * subscope).
   */
  private fun <RequestT, ResponseT> serverMethodDefinition(
    scope: CoroutineScope,
    descriptor: MethodDescriptor<RequestT, ResponseT>,
    implementation: (Flow<RequestT>) -> Flow<ResponseT>
  ): ServerMethodDefinition<RequestT, ResponseT> =
    ServerMethodDefinition.create(
      descriptor,
      serverCallHandler(scope, implementation)
    )

  /**
   * Returns a [ServerCallHandler] that implements an RPC method by running the specified
   * channel-based implementation within the specified [CoroutineScope] (and/or a subscope).
   */
  private fun <RequestT, ResponseT> serverCallHandler(
    scope: CoroutineScope,
    implementation: (Flow<RequestT>) -> Flow<ResponseT>
  ): ServerCallHandler<RequestT, ResponseT> =
    ServerCallHandler {
      call, _ -> serverCallListener(
        scope + GrpcContextElement.current(),
        call,
        implementation
      )
    }

  private fun <RequestT, ResponseT> serverCallListener(
    scope: CoroutineScope,
    call: ServerCall<RequestT, ResponseT>,
    implementation: (Flow<RequestT>) -> Flow<ResponseT>
  ): ServerCall.Listener<RequestT> {
    call.sendHeaders(GrpcMetadata())

    val readiness = Readiness { call.isReady }
    val requestsChannel = Channel<RequestT>(1)

    val requestsStarted = AtomicBoolean(false) // enforces read-once

    val requests = flow<RequestT> {
      check(requestsStarted.compareAndSet(false, true)) {
        "requests flow can only be collected once"
      }

      call.request(1)
      try {
        for (request in requestsChannel) {
          emit(request)
          call.request(1)
        }
      } catch (e: Exception) {
        requestsChannel.cancel(
          CancellationException("Exception thrown while collecting requests", e)
        )
        call.request(1) // make sure we don't cause backpressure
        throw e
      }
    }

    val rpcJob = scope.async(
      CoroutineName("${call.methodDescriptor.fullMethodName} implementation")
    ) {
      thrownOrNull {
        implementation(requests).collect {
          readiness.suspendUntilReady()
          call.sendMessage(it)
        }
      }
    }

    rpcJob.invokeOnCompletion { cause ->
      val closeStatus = when (val failure = cause ?: rpcJob.doneValue) {
        null -> Status.OK
        is CancellationException -> Status.CANCELLED.withCause(failure)
        else -> Status.fromThrowable(failure)
      }
      call.close(closeStatus, GrpcMetadata())
    }

    return object: ServerCall.Listener<RequestT>() {
      var isReceiving = true

      override fun onCancel() {
        rpcJob.cancel("Cancellation received from client")
      }

      override fun onMessage(message: RequestT) {
        if (isReceiving) {
          try {
            if (!requestsChannel.offer(message)) {
              throw Status.INTERNAL
                .withDescription(
                  "onMessage should never be called when requestsChannel is unready"
                )
                .asException()
            }
          } catch (e: CancellationException) {
            // we don't want any more client input; swallow it
            isReceiving = false
          }
        }
        if (!isReceiving) {
          call.request(1) // do not exert backpressure
        }
      }

      override fun onHalfClose() {
        requestsChannel.close()
      }

      override fun onReady() {
        readiness.onReady()
      }
    }
  }
}
