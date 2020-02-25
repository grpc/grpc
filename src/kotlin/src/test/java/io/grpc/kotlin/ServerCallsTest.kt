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

import com.google.common.truth.Truth.assertThat
import com.google.common.truth.extensions.proto.ProtoTruth.assertThat
import io.grpc.CallOptions
import io.grpc.ClientCall
import io.grpc.Context
import io.grpc.Contexts
import io.grpc.Metadata
import io.grpc.ServerCall
import io.grpc.ServerCallHandler
import io.grpc.ServerInterceptor
import io.grpc.Status
import io.grpc.StatusException
import io.grpc.StatusRuntimeException
import io.grpc.examples.helloworld.GreeterGrpc
import io.grpc.examples.helloworld.HelloReply
import io.grpc.examples.helloworld.HelloRequest
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.Job
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.async
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.channels.toList
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.buffer
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.consumeAsFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.flow.produceIn
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import java.util.concurrent.Executors
import kotlin.coroutines.CoroutineContext
import kotlin.coroutines.EmptyCoroutineContext

@RunWith(JUnit4::class)
class ServerCallsTest : AbstractCallsTest() {
  @Test
  fun simpleUnaryMethod() = runBlocking {
    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(this, sayHelloMethod) { request ->
        helloReply("Hello, ${request.name}")
      }
    )

    val stub = GreeterGrpc.newBlockingStub(channel)
    assertThat(stub.sayHello(helloRequest("Steven"))).isEqualTo(helloReply("Hello, Steven"))
    assertThat(stub.sayHello(helloRequest("Pearl"))).isEqualTo(helloReply("Hello, Pearl"))
  }

  @Test
  fun unaryMethodCancellationPropagatedToServer() = runBlocking {
    val requestReceived = Job()
    val cancelled = Job()
    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(this, sayHelloMethod) {
        requestReceived.complete()
        suspendUntilCancelled { cancelled.complete() }
      }
    )

    val stub = GreeterGrpc.newFutureStub(channel)
    val future = stub.sayHello(helloRequest("Garnet"))
    requestReceived.join()
    future.cancel(true)
    cancelled.join()
  }

  @Test
  fun unaryMethodReceivedTooManyRequests() = runBlocking {
    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(this, sayHelloMethod) {
        helloReply("Hello, ${it.name}")
      }
    )
    val call = channel.newCall(sayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()

    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    call.request(1)
    call.sendMessage(helloRequest("Amethyst"))
    call.sendMessage(helloRequest("Pearl"))
    call.halfClose()
    val status = closeStatus.await()
    assertThat(status.code).isEqualTo(Status.Code.INTERNAL)
    assertThat(status.description).contains("received two")
  }

  @Test
  fun unaryMethodReceivedNoRequests() = runBlocking {
    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(this, sayHelloMethod) {
        helloReply("Hello, ${it.name}")
      }
    )
    val call = channel.newCall(sayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()

    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    call.request(1)
    call.halfClose()
    val status = closeStatus.await()
    assertThat(status.code).isEqualTo(Status.Code.INTERNAL)
    assertThat(status.description).contains("received none")
  }

  @Test
  fun unaryMethodThrowsStatusException() = runBlocking {
    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(this, sayHelloMethod) {
        throw StatusException(Status.OUT_OF_RANGE)
      }
    )

    val stub = GreeterGrpc.newBlockingStub(channel)
    val ex = assertThrows<StatusRuntimeException> {
      stub.sayHello(helloRequest("Peridot"))
    }
    assertThat(ex.status.code).isEqualTo(Status.Code.OUT_OF_RANGE)
  }

  class MyException : Exception()

  @Test
  fun unaryMethodThrowsException() = runBlocking {
    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(this, sayHelloMethod) {
        throw MyException()
      }
    )

    val stub = GreeterGrpc.newBlockingStub(channel)
    val ex = assertThrows<StatusRuntimeException> {
      stub.sayHello(helloRequest("Lapis Lazuli"))
    }
    assertThat(ex.status.code).isEqualTo(Status.Code.UNKNOWN)
  }

  @Test
  fun simpleServerStreaming() = runBlocking {
    val channel = makeChannel(
      ServerCalls.serverStreamingServerMethodDefinition(this, serverStreamingSayHelloMethod) {
        it.nameList.asFlow().map { helloReply("Hello, $it") }
      }
    )

    val responses = ClientCalls.serverStreamingRpc(
      channel,
      serverStreamingSayHelloMethod,
      multiHelloRequest("Garnet", "Amethyst", "Pearl")
    )
    assertThat(responses.toList())
      .containsExactly(
        helloReply("Hello, Garnet"),
        helloReply("Hello, Amethyst"),
        helloReply("Hello, Pearl")
      ).inOrder()
  }

  @Test
  fun serverStreamingCancellationPropagatedToServer() = runBlocking {
    val requestReceived = Job()
    val cancelled = Job()
    val channel = makeChannel(
      ServerCalls.serverStreamingServerMethodDefinition(
        this,
        serverStreamingSayHelloMethod
      ) {
        flow {
          requestReceived.complete()
          suspendUntilCancelled { cancelled.complete() }
        }
      }
    )

    val call = channel.newCall(serverStreamingSayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()
    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    call.sendMessage(multiHelloRequest("Steven"))
    call.halfClose()
    requestReceived.join()
    call.cancel("Test cancellation", null)
    cancelled.join()
    assertThat(closeStatus.await().code).isEqualTo(Status.Code.CANCELLED)
  }

  @Test
  fun serverStreamingThrowsStatusException() = runBlocking {
    val channel = makeChannel(
      ServerCalls.serverStreamingServerMethodDefinition(
        this,
        serverStreamingSayHelloMethod
      ) { flow { throw StatusException(Status.OUT_OF_RANGE) } }
    )

    val call = channel.newCall(serverStreamingSayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()
    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    // serverStreamingMethodDefinition waits until the client has definitely sent exactly one
    // message before executing the implementation, so we have to halfClose
    call.sendMessage(multiHelloRequest("Steven"))
    call.halfClose()
    assertThat(closeStatus.await().code).isEqualTo(Status.Code.OUT_OF_RANGE)
  }

  @Test
  fun serverStreamingThrowsException() = runBlocking {
    val channel = makeChannel(
      ServerCalls.serverStreamingServerMethodDefinition(
        this,
        serverStreamingSayHelloMethod
      ) { throw MyException() }
    )

    val call = channel.newCall(serverStreamingSayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()
    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )

    // serverStreamingMethodDefinition waits until the client has definitely sent exactly one
    // message before executing the implementation, so we have to halfClose
    call.sendMessage(multiHelloRequest("Steven"))
    call.halfClose()
    assertThat(closeStatus.await().code).isEqualTo(Status.Code.UNKNOWN)
  }

  @Test
  fun simpleClientStreaming() = runBlocking {
    val channel = makeChannel(
      ServerCalls.clientStreamingServerMethodDefinition(
        this,
        clientStreamingSayHelloMethod
      ) { requests ->
        helloReply(requests.toList().joinToString(separator = ", ", prefix = "Hello, ") { it.name })
      }
    )

    val requestChannel = flowOf(
      helloRequest("Ruby"),
      helloRequest("Sapphire")
    )
    assertThat(
      ClientCalls.clientStreamingRpc(
        channel,
        clientStreamingSayHelloMethod,
        requestChannel
      )
    ).isEqualTo(helloReply("Hello, Ruby, Sapphire"))
  }

  @ExperimentalCoroutinesApi // take
  @Test
  fun clientStreamingDoesntWaitForAllRequests() = runBlocking {
    val channel = makeChannel(
      ServerCalls.clientStreamingServerMethodDefinition(
        this,
        clientStreamingSayHelloMethod
      ) { requests ->
        val (req1, req2) = requests.take(2).toList()
        helloReply("Hello, ${req1.name} and ${req2.name}")
      }
    )

    val requests = flowOf(
      helloRequest("Peridot"),
      helloRequest("Lapis"),
      helloRequest("Jasper"),
      helloRequest("Aquamarine")
    )
    assertThat(
      ClientCalls.clientStreamingRpc(
        channel,
        clientStreamingSayHelloMethod,
        requests
      )
    ).isEqualTo(helloReply("Hello, Peridot and Lapis"))
  }

  @ExperimentalCoroutinesApi // take
  @FlowPreview
  @Test
  fun clientStreamingWhenRequestsCancelledNoBackpressure() = runBlocking {
    val barrier = Job()
    val channel = makeChannel(
      ServerCalls.clientStreamingServerMethodDefinition(
        this,
        clientStreamingSayHelloMethod
      ) { requests ->
        val (req1, req2) = requests.take(2).toList()
        barrier.join()
        helloReply("Hello, ${req1.name} and ${req2.name}")
      }
    )

    val requestChannel = Channel<HelloRequest>()
    val response = async {
      ClientCalls.clientStreamingRpc(
        channel,
        clientStreamingSayHelloMethod,
        requestChannel.consumeAsFlow()
      )
    }
    requestChannel.send(helloRequest("Lapis"))
    requestChannel.send(helloRequest("Peridot"))
    for (i in 1..1000) {
      requestChannel.send(helloRequest("Ruby"))
    }
    barrier.complete()
    assertThat(response.await()).isEqualTo(helloReply("Hello, Lapis and Peridot"))
  }

  @Test
  fun clientStreamingCancellationPropagatedToServer() = runBlocking {
    val requestReceived = Job()
    val cancelled = Job()
    val channel = makeChannel(
      ServerCalls.clientStreamingServerMethodDefinition(
        this,
        clientStreamingSayHelloMethod
      ) {
        it.collect {
          requestReceived.complete()
          suspendUntilCancelled { cancelled.complete() }
        }
        helloReply("Impossible?")
      }
    )

    val call = channel.newCall(clientStreamingSayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()
    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    call.sendMessage(helloRequest("Steven"))
    requestReceived.join()
    call.cancel("Test cancellation", null)
    cancelled.join()
    assertThat(closeStatus.await().code).isEqualTo(Status.Code.CANCELLED)
  }

  @Test
  fun clientStreamingThrowsStatusException() = runBlocking {
    val channel = makeChannel(
      ServerCalls.clientStreamingServerMethodDefinition(
        this,
        clientStreamingSayHelloMethod
      ) { throw StatusException(Status.INVALID_ARGUMENT) }
    )

    val call = channel.newCall(clientStreamingSayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()
    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    call.sendMessage(helloRequest("Steven"))
    assertThat(closeStatus.await().code).isEqualTo(Status.Code.INVALID_ARGUMENT)
  }

  @Test
  fun clientStreamingThrowsException() = runBlocking {
    val channel = makeChannel(
      ServerCalls.clientStreamingServerMethodDefinition(
        this,
        clientStreamingSayHelloMethod
      ) {
        throw MyException()
      }
    )

    val call = channel.newCall(clientStreamingSayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()
    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    call.sendMessage(helloRequest("Steven"))
    assertThat(closeStatus.await().code).isEqualTo(Status.Code.UNKNOWN)
  }

  @ExperimentalCoroutinesApi // onCompletion
  @FlowPreview
  @Test
  fun simpleBidiStreamingPingPong() = runBlocking {
    val channel = makeChannel(
      ServerCalls.bidiStreamingServerMethodDefinition(this, bidiStreamingSayHelloMethod) {
        requests -> requests.map { helloReply("Hello, ${it.name}") }.onCompletion { emit(helloReply("Goodbye")) }
      }
    )

    val requests = Channel<HelloRequest>()
    val responses =
      ClientCalls.bidiStreamingRpc(channel, bidiStreamingSayHelloMethod, requests.consumeAsFlow())
        .produceIn(this)

    requests.send(helloRequest("Garnet"))
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Garnet"))
    requests.send(helloRequest("Steven"))
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Steven"))
    requests.close()
    assertThat(responses.receive()).isEqualTo(helloReply("Goodbye"))
    assertThat(responses.toList()).isEmpty()
  }

  @Test
  fun bidiStreamingCancellationPropagatedToServer() = runBlocking {
    val requestReceived = Job()
    val cancelled = Job()
    val channel = makeChannel(
      ServerCalls.bidiStreamingServerMethodDefinition(
        this,
        bidiStreamingSayHelloMethod
      ) { requests ->
        flow {
          requests.collect {
            requestReceived.complete()
            suspendUntilCancelled { cancelled.complete() }
          }
        }
      }
    )

    val call = channel.newCall(bidiStreamingSayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()
    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    call.sendMessage(helloRequest("Steven"))
    requestReceived.join()
    call.cancel("Test cancellation", null)
    cancelled.join()
    assertThat(closeStatus.await().code).isEqualTo(Status.Code.CANCELLED)
  }

  @Test
  fun bidiStreamingThrowsStatusException() = runBlocking {
    val channel = makeChannel(
      ServerCalls.bidiStreamingServerMethodDefinition(
        this,
        bidiStreamingSayHelloMethod
      ) { flow { throw StatusException(Status.INVALID_ARGUMENT) } }
    )

    val call = channel.newCall(bidiStreamingSayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()
    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    call.sendMessage(helloRequest("Steven"))
    assertThat(closeStatus.await().code).isEqualTo(Status.Code.INVALID_ARGUMENT)
  }

  @Test
  fun bidiStreamingThrowsException() = runBlocking {
    val channel = makeChannel(
      ServerCalls.bidiStreamingServerMethodDefinition(
        this,
        bidiStreamingSayHelloMethod
      ) { throw MyException() }
    )

    val call = channel.newCall(bidiStreamingSayHelloMethod, CallOptions.DEFAULT)
    val closeStatus = CompletableDeferred<Status>()
    call.start(
      object : ClientCall.Listener<HelloReply>() {
        override fun onClose(status: Status, trailers: Metadata) {
          closeStatus.complete(status)
        }
      },
      Metadata()
    )
    call.sendMessage(helloRequest("Steven"))
    assertThat(closeStatus.await().code).isEqualTo(Status.Code.UNKNOWN)
  }

  @Test
  fun rejectNonUnaryMethod() = runBlocking {
    assertThrows<IllegalArgumentException> {
      ServerCalls.unaryServerMethodDefinition(this, bidiStreamingSayHelloMethod) { TODO() }
    }
  }

  @Test
  fun rejectNonClientStreamingMethod() = runBlocking {
    assertThrows<IllegalArgumentException> {
      ServerCalls
        .clientStreamingServerMethodDefinition(this, sayHelloMethod) { TODO() }
    }
  }

  @Test
  fun rejectNonServerStreamingMethod() = runBlocking {
    assertThrows<IllegalArgumentException> {
      ServerCalls
        .serverStreamingServerMethodDefinition(this, sayHelloMethod) { TODO() }
    }
  }

  @Test
  fun rejectNonBidiStreamingMethod() = runBlocking {
    assertThrows<IllegalArgumentException> {
      ServerCalls
        .bidiStreamingServerMethodDefinition(this, sayHelloMethod) { TODO() }
    }
  }

  @Test
  fun unaryContextPropagated() = runBlocking {
    val differentThreadContext: CoroutineContext =
      Executors.newSingleThreadExecutor().asCoroutineDispatcher()
    val contextKey = Context.key<String>("testKey")
    val contextToInject = Context.ROOT.withValue(contextKey, "testValue")

    val interceptor = object : ServerInterceptor {
      override fun <RequestT, ResponseT> interceptCall(
        call: ServerCall<RequestT, ResponseT>,
        headers: Metadata,
        next: ServerCallHandler<RequestT, ResponseT>
      ): ServerCall.Listener<RequestT> {
        return Contexts.interceptCall(
          contextToInject,
          call,
          headers,
          next
        )
      }
    }

    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(this, sayHelloMethod) {
        withContext(differentThreadContext) {
          // Run this in a definitely different thread, just to verify context propagation
          // is WAI.
          assertThat(contextKey.get(Context.current())).isEqualTo("testValue")
          helloReply("Hello, ${it.name}")
        }
      },
      interceptor
    )

    val stub = GreeterGrpc.newBlockingStub(channel)
    assertThat(stub.sayHello(helloRequest("Peridot"))).isEqualTo(helloReply("Hello, Peridot"))
  }

  @Test
  fun serverScopeCancelledDuringRpc() = runBlocking {
    val serverScope = CoroutineScope(EmptyCoroutineContext)
    val serverReceived = Job()
    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(serverScope, sayHelloMethod) {
        serverReceived.complete()
        suspendForever()
      }
    )

    val test = launch {
      val ex = assertThrows<StatusException> {
        ClientCalls.unaryRpc(channel, sayHelloMethod, helloRequest("Greg"))
      }
      assertThat(ex.status.code).isEqualTo(Status.Code.CANCELLED)
    }
    serverReceived.join()
    serverScope.cancel()
    test.join()
  }

  @Test
  fun serverScopeCancelledBeforeRpc() = runBlocking {
    val serverScope = CoroutineScope(EmptyCoroutineContext)
    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(serverScope, sayHelloMethod) {
        suspendForever()
      }
    )

    serverScope.cancel()
    val test = launch {
      val ex = assertThrows<StatusException> {
        ClientCalls.unaryRpc(channel, sayHelloMethod, helloRequest("Greg"))
      }
      assertThat(ex.status.code).isEqualTo(Status.Code.CANCELLED)
    }
    test.join()
  }

  @ExperimentalCoroutinesApi
  @FlowPreview
  @Test
  fun serverStreamingFlowControl() = runBlocking {
    val receiveFirstMessage = Job()
    val receivedFirstMessage = Job()
    val channel = makeChannel(
      ServerCalls.serverStreamingServerMethodDefinition(
        this,
        serverStreamingSayHelloMethod
      ) {
        channelFlow {
          send(helloReply("1st"))
          send(helloReply("2nd"))
          val thirdSend = launch {
            send(helloReply("3rd"))
          }
          delay(200)
          assertThat(thirdSend.isCompleted).isFalse()
          receiveFirstMessage.complete()
          receivedFirstMessage.join()
          thirdSend.join()
        }.buffer(Channel.RENDEZVOUS)
      }
    )

    val responses = produce<HelloReply> {
      ClientCalls.serverStreamingRpc(
        channel,
        serverStreamingSayHelloMethod,
        multiHelloRequest()
      ).collect { send(it) }
    }
    receiveFirstMessage.join()
    assertThat(responses.receive()).isEqualTo(helloReply("1st"))
    receivedFirstMessage.complete()
    assertThat(responses.toList()).containsExactly(helloReply("2nd"), helloReply("3rd"))
  }

  @Test
  fun contextPreservation() = runBlocking {
    val contextKey = Context.key<String>("foo")
    val channel = makeChannel(
      ServerCalls.unaryServerMethodDefinition(
        this,
        sayHelloMethod
      ) {
        assertThat(contextKey.get()).isEqualTo("bar")
        helloReply("Hello!")
      },
      object : ServerInterceptor {
        override fun <ReqT, RespT> interceptCall(
          call: ServerCall<ReqT, RespT>,
          headers: Metadata,
          next: ServerCallHandler<ReqT, RespT>
        ): ServerCall.Listener<ReqT> =
          Contexts.interceptCall(
            Context.current().withValue(contextKey, "bar"),
            call,
            headers,
            next
          )
      }
    )
    assertThat(
      ClientCalls.unaryRpc(channel, sayHelloMethod, helloRequest(""))
    ).isEqualTo(helloReply("Hello!"))
  }
}
