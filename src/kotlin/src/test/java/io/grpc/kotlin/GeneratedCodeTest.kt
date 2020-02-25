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
import io.grpc.Status
import io.grpc.StatusException
import io.grpc.examples.helloworld.GreeterGrpcKt.GreeterCoroutineImplBase
import io.grpc.examples.helloworld.GreeterGrpcKt.GreeterCoroutineStub
import io.grpc.examples.helloworld.HelloReply
import io.grpc.examples.helloworld.HelloRequest
import io.grpc.examples.helloworld.MultiHelloRequest
import io.grpc.examples.helloworld.helloReply
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.channels.ReceiveChannel
import kotlinx.coroutines.channels.SendChannel
import kotlinx.coroutines.channels.toList
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.consumeAsFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.produceIn
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import java.util.concurrent.TimeUnit

@RunWith(JUnit4::class)
class GeneratedCodeTest : AbstractCallsTest() {
  @Test
  fun simpleUnary() {
    val server = object : GreeterCoroutineImplBase() {
      override suspend fun sayHello(request: HelloRequest): HelloReply {
        return helloReply { message = "Hello, ${request.name}!" }
      }
    }
    val channel = makeChannel(server)
    val stub = GreeterCoroutineStub(channel)

    runBlocking {
      assertThat(stub.sayHello(helloRequest("Steven")))
        .isEqualTo(helloReply("Hello, Steven!"))
    }
  }

  @Test
  fun unaryServerDoesNotRespondGrpcTimeout() = runBlocking {
    val serverCancelled = Job()

    val channel = makeChannel(object : GreeterCoroutineImplBase() {
      override suspend fun sayHello(request: HelloRequest): HelloReply {
        suspendUntilCancelled {
          serverCancelled.complete()
        }
      }
    })

    val stub = GreeterCoroutineStub(channel).withDeadlineAfter(100, TimeUnit.MILLISECONDS)

    val ex = assertThrows<StatusException> {
      stub.sayHello(helloRequest("Topaz"))
    }
    assertThat(ex.status.code).isEqualTo(Status.Code.DEADLINE_EXCEEDED)
    serverCancelled.join()
  }

  @Test
  fun unaryClientCancellation() {
    val helloReceived = Job()
    val helloCancelled = Job()
    val helloChannel = makeChannel(object : GreeterCoroutineImplBase() {
      override suspend fun sayHello(request: HelloRequest): HelloReply {
        helloReceived.complete()
        suspendUntilCancelled {
          helloCancelled.complete()
        }
      }
    })
    val helloStub = GreeterCoroutineStub(helloChannel)

    runBlocking {
      val result = async {
        val request = helloRequest("Steven")
        helloStub.sayHello(request)
      }
      helloReceived.join()
      result.cancel()
      helloCancelled.join()
    }
  }

  @Test
  fun unaryMethodThrowsStatusException() = runBlocking {
    val channel = makeChannel(
      object : GreeterCoroutineImplBase() {
        override suspend fun sayHello(request: HelloRequest): HelloReply {
          throw StatusException(Status.PERMISSION_DENIED)
        }
      }
    )

    val stub = GreeterCoroutineStub(channel)
    val ex = assertThrows<StatusException> {
      stub.sayHello(helloRequest("Peridot"))
    }
    assertThat(ex.status.code).isEqualTo(Status.Code.PERMISSION_DENIED)
  }

  @Test
  fun unaryMethodThrowsException() = runBlocking {
    val channel = makeChannel(
      object : GreeterCoroutineImplBase() {
        override suspend fun sayHello(request: HelloRequest): HelloReply {
          throw IllegalArgumentException()
        }
      }
    )

    val stub = GreeterCoroutineStub(channel)
    val ex = assertThrows<StatusException> {
      stub.sayHello(helloRequest("Peridot"))
    }
    assertThat(ex.status.code).isEqualTo(Status.Code.UNKNOWN)
  }

  @Test
  fun simpleClientStreamingRpc() = runBlocking {
    val channel = makeChannel(object : GreeterCoroutineImplBase() {
      override suspend fun clientStreamSayHello(requests: Flow<HelloRequest>): HelloReply {
        return helloReply {
          message = requests.toList().joinToString(prefix = "Hello, ", separator = ", ") { it.name }
        }
      }
    })

    val stub = GreeterCoroutineStub(channel)
    val requests = flowOf(
      helloRequest("Peridot"),
      helloRequest("Lapis")
    )
    val response = async { stub.clientStreamSayHello(requests) }
    assertThat(response.await()).isEqualTo(helloReply("Hello, Peridot, Lapis"))
  }

  @FlowPreview
  @Test
  fun clientStreamingRpcCancellation() = runBlocking {
    val serverReceived = Job()
    val serverCancelled = Job()
    val channel = makeChannel(object : GreeterCoroutineImplBase() {
      override suspend fun clientStreamSayHello(requests: Flow<HelloRequest>): HelloReply {
        requests.collect {
          serverReceived.complete()
          suspendUntilCancelled { serverCancelled.complete() }
        }
        throw AssertionError("unreachable")
      }
    })

    val stub = GreeterCoroutineStub(channel)
    val requests = Channel<HelloRequest>()
    val response = async {
      stub.clientStreamSayHello(requests.consumeAsFlow())
    }
    requests.send(helloRequest("Aquamarine"))
    serverReceived.join()
    response.cancel()
    serverCancelled.join()
    assertThrows<CancellationException> {
      requests.send(helloRequest("John"))
    }
  }

  @Test
  fun clientStreamingRpcThrowsStatusException() = runBlocking {
    val channel = makeChannel(object : GreeterCoroutineImplBase() {
      override suspend fun clientStreamSayHello(requests: Flow<HelloRequest>): HelloReply {
        throw StatusException(Status.PERMISSION_DENIED)
      }
    })
    val stub = GreeterCoroutineStub(channel)

    val ex = assertThrows<StatusException> {
      stub.clientStreamSayHello(flowOf<HelloRequest>())
    }
    assertThat(ex.status.code).isEqualTo(Status.Code.PERMISSION_DENIED)
  }

  @Test
  fun simpleServerStreamingRpc() = runBlocking {
    val channel = makeChannel(object : GreeterCoroutineImplBase() {
      override fun serverStreamSayHello(request: MultiHelloRequest): Flow<HelloReply> {
        return request.nameList.asFlow().map { helloReply("Hello, $it") }
      }
    })

    val responses = GreeterCoroutineStub(channel).serverStreamSayHello(
      multiHelloRequest("Garnet", "Amethyst", "Pearl")
    )

    assertThat(responses.toList())
      .containsExactly(
        helloReply("Hello, Garnet"),
        helloReply("Hello, Amethyst"),
        helloReply("Hello, Pearl")
      )
      .inOrder()
  }

  @FlowPreview
  @Test
  fun serverStreamingRpcCancellation() = runBlocking {
    val serverCancelled = Job()
    val serverReceived = Job()

    val channel = makeChannel(object : GreeterCoroutineImplBase() {
      override fun serverStreamSayHello(request: MultiHelloRequest): Flow<HelloReply> {
        return flow {
          serverReceived.complete()
          suspendUntilCancelled {
            serverCancelled.complete()
          }
        }
      }
    })

    val response = GreeterCoroutineStub(channel).serverStreamSayHello(
      multiHelloRequest("Topaz", "Aquamarine")
    ).produceIn(this)
    serverReceived.join()
    response.cancel()
    serverCancelled.join()
  }

  @FlowPreview
  @Test
  fun bidiPingPong() = runBlocking {
    val channel = makeChannel(object : GreeterCoroutineImplBase() {
      override fun bidiStreamSayHello(requests: Flow<HelloRequest>): Flow<HelloReply> {
        return requests.map { helloReply("Hello, ${it.name}") }
      }
    })

    val requests = Channel<HelloRequest>()
    val responses = GreeterCoroutineStub(channel).bidiStreamSayHello(requests.consumeAsFlow()).produceIn(this)

    requests.send(helloRequest("Steven"))
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Steven"))
    requests.send(helloRequest("Garnet"))
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Garnet"))
    requests.close()
    assertThat(responses.toList()).isEmpty()
  }

  @ExperimentalCoroutinesApi
  @FlowPreview
  @Test
  fun bidiStreamingRpcReturnsEarly() = runBlocking {
    val channel = makeChannel(object : GreeterCoroutineImplBase() {
      override fun bidiStreamSayHello(requests: Flow<HelloRequest>): Flow<HelloReply> {
        return requests.take(2).map { helloReply("Hello, ${it.name}") }
      }
    })

    val stub = GreeterCoroutineStub(channel)
    val requests = Channel<HelloRequest>()
    val responses = stub.bidiStreamSayHello(requests.consumeAsFlow()).produceIn(this)
    requests.send(helloRequest("Peridot"))
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Peridot"))
    requests.send(helloRequest("Lapis"))
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Lapis"))
    assertThat(responses.toList()).isEmpty()
    try {
      requests.send(helloRequest("Jasper"))
    } catch (allowed: CancellationException) {}
  }

  @Test
  fun serverScopeCancelledDuringRpc() = runBlocking {
    val serverJob = Job()
    val serverReceived = Job()
    val channel = makeChannel(
      object : GreeterCoroutineImplBase(serverJob) {
        override suspend fun sayHello(request: HelloRequest): HelloReply {
          serverReceived.complete()
          suspendUntilCancelled { /* do nothing */ }
        }
      }
    )

    val stub = GreeterCoroutineStub(channel)
    val test = launch {
      val ex = assertThrows<StatusException> {
        stub.sayHello(helloRequest("Greg"))
      }
      assertThat(ex.status.code).isEqualTo(Status.Code.CANCELLED)
    }
    serverReceived.join()
    serverJob.cancel()
    test.join()
  }

  @Test
  fun serverScopeCancelledBeforeRpc() = runBlocking {
    val serverJob = Job()
    val channel = makeChannel(
      object : GreeterCoroutineImplBase(serverJob) {
        override suspend fun sayHello(request: HelloRequest): HelloReply {
          suspendUntilCancelled { /* do nothing */ }
        }
      }
    )

    serverJob.cancel()
    val stub = GreeterCoroutineStub(channel)
    val ex = assertThrows<StatusException> {
      stub.sayHello(helloRequest("Greg"))
    }
    assertThat(ex.status.code).isEqualTo(Status.Code.CANCELLED)
  }
}
