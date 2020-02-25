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
import io.grpc.examples.helloworld.HelloRequest
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.channels.ReceiveChannel
import kotlinx.coroutines.channels.toList
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.buffer
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.consumeAsFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.transform
import kotlinx.coroutines.launch
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4

/** Tests for the flow control of the Kotlin gRPC APIs. */
@RunWith(JUnit4::class)
class FlowControlTest : AbstractCallsTest() {
  private fun <T> Flow<T>.produceUnbuffered(scope: CoroutineScope): ReceiveChannel<T> {
    return scope.produce<T> {
      collect { send(it) }
    }
  }

  @FlowPreview
  @Test
  fun bidiPingPongFlowControl() = runBlocking {
    val channel = makeChannel(
      ServerCalls.bidiStreamingServerMethodDefinition(
        scope = this,
        descriptor = bidiStreamingSayHelloMethod,
        implementation = { requests -> requests.map { helloReply("Hello, ${it.name}") } }
      )
    )
    val requests = Channel<HelloRequest>()
    val responses =
      ClientCalls.bidiStreamingRpc(
        channel = channel,
        requests = requests.consumeAsFlow(),
        method = bidiStreamingSayHelloMethod
      ).produceUnbuffered(this)
    requests.send(helloRequest("Garnet"))
    requests.send(helloRequest("Amethyst"))
    val third = launch { requests.send(helloRequest("Steven")) }
    delay(200) // wait for everything to work its way through the system
    assertThat(third.isCompleted).isFalse()
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Garnet"))
    third.join() // pulling one element allows the cycle to advance
    responses.cancel()
  }

  @ExperimentalCoroutinesApi // buffer
  @FlowPreview
  @Test
  fun bidiPingPongFlowControlExpandedServerBuffer() = runBlocking {
    val channel = makeChannel(
      ServerCalls.bidiStreamingServerMethodDefinition(
        scope = this,
        descriptor = bidiStreamingSayHelloMethod,
        implementation = {
          requests -> requests.buffer(Channel.RENDEZVOUS).map { helloReply("Hello, ${it.name}") }
        }
      )
    )
    val requests = Channel<HelloRequest>()
    val responses = ClientCalls.bidiStreamingRpc(
      channel = channel,
      requests = requests.consumeAsFlow(),
      method = bidiStreamingSayHelloMethod
    ).produceUnbuffered(this)
    requests.send(helloRequest("Garnet"))
    requests.send(helloRequest("Amethyst"))
    requests.send(helloRequest("Pearl"))
    val fourth = launch { requests.send(helloRequest("Pearl")) }
    delay(200) // wait for everything to work its way through the system
    assertThat(fourth.isCompleted).isFalse()
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Garnet"))
    fourth.join() // pulling one element allows the cycle to advance
    responses.cancel()
  }

  @FlowPreview
  @Test
  fun bidiPingPongFlowControlServerDrawsMultipleRequests() = runBlocking {
    fun <T: Any> Flow<T>.pairOff(): Flow<Pair<T, T>> = flow {
      var odd: T? = null
      collect {
        val o = odd
        if (o == null) {
          odd = it
        } else {
          emit(Pair(o, it))
          odd = null
        }
      }
    }

    val channel = makeChannel(
      ServerCalls.bidiStreamingServerMethodDefinition(
        scope = this,
        descriptor = bidiStreamingSayHelloMethod,
        implementation = { requests ->
          requests.pairOff().map { (a, b) -> helloReply("Hello, ${a.name} and ${b.name}") }
        }
      )
    )
    val requests = Channel<HelloRequest>()
    val responses = ClientCalls.bidiStreamingRpc(
      channel = channel,
      requests = requests.consumeAsFlow(),
      method = bidiStreamingSayHelloMethod
    ).produceUnbuffered(this)
    requests.send(helloRequest("Garnet"))
    requests.send(helloRequest("Amethyst"))
    requests.send(helloRequest("Pearl"))
    requests.send(helloRequest("Steven"))
    val fourth = launch { requests.send(helloRequest("Onion")) }
    delay(300) // wait for everything to work its way through the system
    assertThat(fourth.isCompleted).isFalse()
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Garnet and Amethyst"))
    fourth.join() // pulling one element allows the cycle to advance
    requests.send(helloRequest("Rainbow 2.0"))
    requests.close()
    assertThat(responses.toList()).containsExactly(
      helloReply("Hello, Pearl and Steven"), helloReply("Hello, Onion and Rainbow 2.0")
    )
  }

  @ExperimentalCoroutinesApi // transform
  @FlowPreview
  @Test
  fun bidiPingPongFlowControlServerSendsMultipleResponses() = runBlocking {
    val channel = makeChannel(
      ServerCalls.bidiStreamingServerMethodDefinition(
        scope = this,
        descriptor = bidiStreamingSayHelloMethod,
        implementation = { requests ->
          requests.transform {
            emit(helloReply("Hello, ${it.name}"))
            emit(helloReply("Goodbye, ${it.name}"))
          }
        }
      )
    )
    val requests = Channel<HelloRequest>()
    val responses = ClientCalls.bidiStreamingRpc(
      channel = channel,
      requests = requests.consumeAsFlow(),
      method = bidiStreamingSayHelloMethod
    ).produceUnbuffered(this)
    requests.send(helloRequest("Garnet"))
    val second = launch { requests.send(helloRequest("Pearl")) }
    delay(200) // wait for everything to work its way through the system
    assertThat(second.isCompleted).isFalse()
    assertThat(responses.receive()).isEqualTo(helloReply("Hello, Garnet"))
    second.join()
    assertThat(responses.receive()).isEqualTo(helloReply("Goodbye, Garnet"))
    responses.cancel()
  }
}
