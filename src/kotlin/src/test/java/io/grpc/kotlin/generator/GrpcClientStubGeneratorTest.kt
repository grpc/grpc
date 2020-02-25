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

package io.grpc.kotlin.generator

import com.google.common.io.Resources
import com.google.protobuf.Descriptors.MethodDescriptor
import com.google.protobuf.Descriptors.ServiceDescriptor
import com.google.protobuf.kotlin.protoc.GeneratorConfig
import com.google.protobuf.kotlin.protoc.JavaPackagePolicy
import com.google.protobuf.kotlin.protoc.testing.assertThat
import io.grpc.examples.helloworld.HelloWorldProto
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.Paths

@RunWith(JUnit4::class)
class GrpcClientStubGeneratorTest {
  companion object {
    private val generator =
      GrpcClientStubGenerator(GeneratorConfig(JavaPackagePolicy.OPEN_SOURCE, false))
    private val greeterServiceDescriptor: ServiceDescriptor =
      HelloWorldProto.getDescriptor().findServiceByName("Greeter")
    private val unaryMethodDescriptor: MethodDescriptor =
      greeterServiceDescriptor.findMethodByName("SayHello")
    private val bidiStreamingMethodDescriptor: MethodDescriptor =
      greeterServiceDescriptor.findMethodByName("BidiStreamSayHello")
  }

  @Test
  fun generateUnaryRpcStub() {
    assertThat(generator.generateRpcStub(unaryMethodDescriptor)).generates(
      """
      /**
       * Executes this RPC and returns the response message, suspending until the RPC completes
       * with [`Status.OK`][io.grpc.Status].  If the RPC completes with another status, a corresponding
       * [io.grpc.StatusException] is thrown.  If this coroutine is cancelled, the RPC is also cancelled
       * with the corresponding exception as a cause.
       *
       * @param request The request message to send to the server.
       *
       * @return The single response from the server.
       */
      suspend fun sayHello(request: io.grpc.examples.helloworld.HelloRequest, headers: io.grpc.Metadata = io.grpc.Metadata()): io.grpc.examples.helloworld.HelloReply = io.grpc.kotlin.ClientCalls.unaryRpc(
        channel,
        io.grpc.examples.helloworld.GreeterGrpc.getSayHelloMethod(),
        request,
        callOptions,
        headers
      )
      """.trimIndent()
    )
  }

  @Test
  fun generateBidiStreamingRpcStub() {
    assertThat(generator.generateRpcStub(bidiStreamingMethodDescriptor)).generates(
      """
      /**
       * Returns a [kotlinx.coroutines.flow.Flow] that, when collected, executes this RPC and emits responses from the
       * server as they arrive.  That flow finishes normally if the server closes its response with
       * [`Status.OK`][io.grpc.Status], and fails by throwing a [io.grpc.StatusException] otherwise.  If
       * collecting the flow downstream fails exceptionally (including via cancellation), the RPC
       * is cancelled with that exception as a cause.
       *
       * The [kotlinx.coroutines.flow.Flow] of requests is collected once each time the [kotlinx.coroutines.flow.Flow] of responses is
       * collected. If collection of the [kotlinx.coroutines.flow.Flow] of responses completes normally or
       * exceptionally before collection of `requests` completes, the collection of
       * `requests` is cancelled.  If the collection of `requests` completes
       * exceptionally for any other reason, then the collection of the [kotlinx.coroutines.flow.Flow] of responses
       * completes exceptionally for the same reason and the RPC is cancelled with that reason.
       *
       * @param requests A [kotlinx.coroutines.flow.Flow] of request messages.
       *
       * @return A flow that, when collected, emits the responses from the server.
       */
      fun bidiStreamSayHello(requests: kotlinx.coroutines.flow.Flow<io.grpc.examples.helloworld.HelloRequest>, headers: io.grpc.Metadata = io.grpc.Metadata()): kotlinx.coroutines.flow.Flow<io.grpc.examples.helloworld.HelloReply> = io.grpc.kotlin.ClientCalls.bidiStreamingRpc(
        channel,
        io.grpc.examples.helloworld.GreeterGrpc.getBidiStreamSayHelloMethod(),
        requests,
        callOptions,
        headers
      )
      """.trimIndent()
    )
  }

  @Test
  fun generateServiceStub() {
    val expectedFileContents = Files.readAllLines(
      Paths.get(
        "src/test/java/io/grpc/kotlin/generator",
        "GreeterCoroutineStub.expected"
      ),
      StandardCharsets.UTF_8
    )

    assertThat(generator.generate(greeterServiceDescriptor)).generatesEnclosed(
      expectedFileContents.joinToString("\n") + "\n"
    )
  }
}
