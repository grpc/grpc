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
import com.google.common.truth.Truth.assertThat
import com.google.protobuf.Descriptors.MethodDescriptor
import com.google.protobuf.kotlin.protoc.ClassSimpleName
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
class CoroutineServerImplGeneratorTest {
  private val serviceDescriptor =
    HelloWorldProto.getDescriptor().findServiceByName("Greeter")
  private val config = GeneratorConfig(JavaPackagePolicy.OPEN_SOURCE, false)
  private val generator = GrpcCoroutineServerGenerator(config)
  private val unarySayHelloDescriptor: MethodDescriptor =
    serviceDescriptor.findMethodByName("SayHello")
  private val clientStreamingSayHelloDescriptor: MethodDescriptor =
    serviceDescriptor.findMethodByName("ClientStreamSayHello")
  private val serverStreamingSayHelloDescriptor: MethodDescriptor =
    serviceDescriptor.findMethodByName("ServerStreamSayHello")
  private val bidiStreamingSayHelloDescriptor: MethodDescriptor =
    serviceDescriptor.findMethodByName("BidiStreamSayHello")
  private val stubSimpleName = ClassSimpleName("ThisImpl")

  @Test
  fun unaryImplStub() {
    val stub = generator.serviceMethodStub(unarySayHelloDescriptor)
    assertThat(stub.methodSpec).generates("""
      /**
       * Returns the response to an RPC for helloworld.Greeter.SayHello.
       *
       * If this method fails with a [io.grpc.StatusException], the RPC will fail with the corresponding
       * [io.grpc.Status].  If this method fails with a [java.util.concurrent.CancellationException], the RPC will fail
       * with status `Status.CANCELLED`.  If this method fails for any other reason, the RPC will
       * fail with `Status.UNKNOWN` with the exception as a cause.
       *
       * @param request The request from the client.
       */
      open suspend fun sayHello(request: io.grpc.examples.helloworld.HelloRequest): io.grpc.examples.helloworld.HelloReply = throw io.grpc.StatusException(io.grpc.Status.UNIMPLEMENTED.withDescription("Method helloworld.Greeter.SayHello is unimplemented"))
    """.trimIndent())
    assertThat(stub.serverMethodDef.toString()).isEqualTo("""
      io.grpc.kotlin.ServerCalls.unaryServerMethodDefinition(
        scope = this,
        descriptor = io.grpc.examples.helloworld.GreeterGrpc.getSayHelloMethod(),
        implementation = ::sayHello
      )
    """.trimIndent())
  }

  @Test
  fun clientStreamingImplStub() {
    val stub = generator.serviceMethodStub(clientStreamingSayHelloDescriptor)
    assertThat(stub.methodSpec).generates("""
      /**
       * Returns the response to an RPC for helloworld.Greeter.ClientStreamSayHello.
       *
       * If this method fails with a [io.grpc.StatusException], the RPC will fail with the corresponding
       * [io.grpc.Status].  If this method fails with a [java.util.concurrent.CancellationException], the RPC will fail
       * with status `Status.CANCELLED`.  If this method fails for any other reason, the RPC will
       * fail with `Status.UNKNOWN` with the exception as a cause.
       *
       * @param requests A [kotlinx.coroutines.flow.Flow] of requests from the client.  This flow can be
       *        collected only once and throws [java.lang.IllegalStateException] on attempts to collect
       *        it more than once.
       */
      open suspend fun clientStreamSayHello(requests: kotlinx.coroutines.flow.Flow<io.grpc.examples.helloworld.HelloRequest>): io.grpc.examples.helloworld.HelloReply = throw io.grpc.StatusException(io.grpc.Status.UNIMPLEMENTED.withDescription("Method helloworld.Greeter.ClientStreamSayHello is unimplemented"))
    """.trimIndent())
    assertThat(stub.serverMethodDef.toString()).isEqualTo("""
      io.grpc.kotlin.ServerCalls.clientStreamingServerMethodDefinition(
        scope = this,
        descriptor = io.grpc.examples.helloworld.GreeterGrpc.getClientStreamSayHelloMethod(),
        implementation = ::clientStreamSayHello
      )
    """.trimIndent())
  }

  @Test
  fun serverStreamingImplStub() {
    val stub = generator.serviceMethodStub(serverStreamingSayHelloDescriptor)
    assertThat(stub.methodSpec).generates("""
      /**
       * Returns a [kotlinx.coroutines.flow.Flow] of responses to an RPC for helloworld.Greeter.ServerStreamSayHello.
       *
       * If creating or collecting the returned flow fails with a [io.grpc.StatusException], the RPC
       * will fail with the corresponding [io.grpc.Status].  If it fails with a
       * [java.util.concurrent.CancellationException], the RPC will fail with status `Status.CANCELLED`.  If creating
       * or collecting the returned flow fails for any other reason, the RPC will fail with
       * `Status.UNKNOWN` with the exception as a cause.
       *
       * @param request The request from the client.
       */
      open fun serverStreamSayHello(request: io.grpc.examples.helloworld.MultiHelloRequest): kotlinx.coroutines.flow.Flow<io.grpc.examples.helloworld.HelloReply> = throw io.grpc.StatusException(io.grpc.Status.UNIMPLEMENTED.withDescription("Method helloworld.Greeter.ServerStreamSayHello is unimplemented"))
    """.trimIndent())
    assertThat(stub.serverMethodDef.toString()).isEqualTo("""
      io.grpc.kotlin.ServerCalls.serverStreamingServerMethodDefinition(
        scope = this,
        descriptor = io.grpc.examples.helloworld.GreeterGrpc.getServerStreamSayHelloMethod(),
        implementation = ::serverStreamSayHello
      )
    """.trimIndent())
  }

  @Test
  fun bidiStreamingImplStub() {
    val stub = generator.serviceMethodStub(bidiStreamingSayHelloDescriptor)
    assertThat(stub.methodSpec).generates("""
      /**
       * Returns a [kotlinx.coroutines.flow.Flow] of responses to an RPC for helloworld.Greeter.BidiStreamSayHello.
       *
       * If creating or collecting the returned flow fails with a [io.grpc.StatusException], the RPC
       * will fail with the corresponding [io.grpc.Status].  If it fails with a
       * [java.util.concurrent.CancellationException], the RPC will fail with status `Status.CANCELLED`.  If creating
       * or collecting the returned flow fails for any other reason, the RPC will fail with
       * `Status.UNKNOWN` with the exception as a cause.
       *
       * @param requests A [kotlinx.coroutines.flow.Flow] of requests from the client.  This flow can be
       *        collected only once and throws [java.lang.IllegalStateException] on attempts to collect
       *        it more than once.
       */
      open fun bidiStreamSayHello(requests: kotlinx.coroutines.flow.Flow<io.grpc.examples.helloworld.HelloRequest>): kotlinx.coroutines.flow.Flow<io.grpc.examples.helloworld.HelloReply> = throw io.grpc.StatusException(io.grpc.Status.UNIMPLEMENTED.withDescription("Method helloworld.Greeter.BidiStreamSayHello is unimplemented"))
    """.trimIndent())
    assertThat(stub.serverMethodDef.toString()).isEqualTo("""
      io.grpc.kotlin.ServerCalls.bidiStreamingServerMethodDefinition(
        scope = this,
        descriptor = io.grpc.examples.helloworld.GreeterGrpc.getBidiStreamSayHelloMethod(),
        implementation = ::bidiStreamSayHello
      )
    """.trimIndent())
  }

  @Test
  fun fullImpl() {
    val type = generator.generate(serviceDescriptor)
    val expectedFileContents = Files.readAllLines(
      Paths.get(
        "src/test/java/io/grpc/kotlin/generator",
        "GreeterCoroutineImplBase.expected"
      ),
      StandardCharsets.UTF_8
    )
    assertThat(type).generatesEnclosed(
      expectedFileContents.joinToString("\n") + "\n"
    )
  }
}
