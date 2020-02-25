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

import com.google.common.util.concurrent.MoreExecutors
import io.grpc.BindableService
import io.grpc.Context
import io.grpc.ManagedChannel
import io.grpc.MethodDescriptor
import io.grpc.ServerBuilder
import io.grpc.ServerCallHandler
import io.grpc.ServerInterceptor
import io.grpc.ServerInterceptors
import io.grpc.ServerMethodDefinition
import io.grpc.ServerServiceDefinition
import io.grpc.ServiceDescriptor
import io.grpc.examples.helloworld.GreeterGrpc
import io.grpc.examples.helloworld.HelloReply
import io.grpc.examples.helloworld.HelloRequest
import io.grpc.examples.helloworld.MultiHelloRequest
import io.grpc.inprocess.InProcessChannelBuilder
import io.grpc.inprocess.InProcessServerBuilder
import io.grpc.testing.GrpcCleanupRule
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.channels.ReceiveChannel
import kotlinx.coroutines.channels.SendChannel
import kotlinx.coroutines.debug.junit4.CoroutinesTimeout
import kotlinx.coroutines.launch
import org.junit.After
import org.junit.Before
import org.junit.Rule

abstract class AbstractCallsTest {
  companion object {
    fun helloRequest(name: String): HelloRequest = HelloRequest.newBuilder().setName(name).build()
    fun helloReply(message: String): HelloReply = HelloReply.newBuilder().setMessage(message).build()
    fun multiHelloRequest(vararg name: String): MultiHelloRequest =
      MultiHelloRequest.newBuilder().addAllName(name.asList()).build()

    val sayHelloMethod: MethodDescriptor<HelloRequest, HelloReply> =
      GreeterGrpc.getSayHelloMethod()
    val clientStreamingSayHelloMethod: MethodDescriptor<HelloRequest, HelloReply> =
      GreeterGrpc.getClientStreamSayHelloMethod()
    val serverStreamingSayHelloMethod: MethodDescriptor<MultiHelloRequest, HelloReply> =
      GreeterGrpc.getServerStreamSayHelloMethod()
    val bidiStreamingSayHelloMethod: MethodDescriptor<HelloRequest, HelloReply> =
      GreeterGrpc.getBidiStreamSayHelloMethod()
    val greeterService: ServiceDescriptor = GreeterGrpc.getServiceDescriptor()

    fun <E> CoroutineScope.produce(
      block: suspend SendChannel<E>.() -> Unit
    ): ReceiveChannel<E> {
      val channel = Channel<E>()
      launch {
        channel.block()
        channel.close()
      }
      return channel
    }

    suspend fun suspendForever(): Nothing {
      suspendUntilCancelled {
        // do nothing
      }
    }

    suspend fun suspendUntilCancelled(onCancelled: (CancellationException) -> Unit): Nothing {
      val deferred = Job()
      try {
        deferred.join()
      } catch (c: CancellationException) {
        onCancelled(c)
        throw c
      }
      throw AssertionError("Unreachable")
    }

    fun whenContextIsCancelled(onCancelled: () -> Unit) {
      Context.current().withCancellation().addListener(
        Context.CancellationListener { onCancelled() },
        MoreExecutors.directExecutor()
      )
    }
  }

  @get:Rule
  val timeout = CoroutinesTimeout.seconds(10)

  // We want the coroutines timeout to come first, because it comes with useful debug logs.
  @get:Rule
  val grpcCleanup = GrpcCleanupRule().setTimeout(11, TimeUnit.SECONDS)

  lateinit var channel: ManagedChannel

  private lateinit var executor: ExecutorService

  private val context: CoroutineContext
    get() = executor.asCoroutineDispatcher()

  @Before
  fun setUp() {
    executor = Executors.newFixedThreadPool(10)
  }

  @After
  fun tearDown() {
    executor.shutdown()
    if (this::channel.isInitialized) {
      channel.shutdownNow()
    }
  }

  inline fun <reified E : Exception> assertThrows(
    callback: () -> Unit
  ): E {
    var ex: Exception? = null
    try {
      callback()
    } catch (e: Exception) {
      ex = e
    }
    if (ex is E) {
      return ex
    } else {
      throw Error("Expected an ${E::class.qualifiedName}", ex)
    }
  }

  /** Generates a channel to a Greeter server with the specified implementation. */
  fun makeChannel(impl: BindableService): ManagedChannel {
    val serverName = InProcessServerBuilder.generateName()

    grpcCleanup.register(
      InProcessServerBuilder.forName(serverName)
        .run { this as ServerBuilder<*> } // workaround b/123879662
        .executor(executor)
        .addService(impl)
        .build()
        .start()
    )

    return grpcCleanup.register(
      InProcessChannelBuilder
        .forName(serverName)
        .run { this as io.grpc.ManagedChannelBuilder<*> } // workaround b/123879662
        .executor(executor)
        .build()
    )
  }

  fun makeChannel(
    impl: ServerMethodDefinition<*, *>,
    vararg interceptors: ServerInterceptor
  ): ManagedChannel =
    makeChannel(
      BindableService {
        val builder = ServerServiceDefinition.builder(greeterService)
        for (method in greeterService.methods) {
          if (method == impl.methodDescriptor) {
            builder.addMethod(impl)
          } else {
            builder.addMethod(method, ServerCallHandler { _, _ -> TODO() })
          }
        }
        ServerInterceptors.intercept(builder.build(), *interceptors)
      }
    )

  fun <R> runBlocking(block: suspend CoroutineScope.() -> R): Unit =
    kotlinx.coroutines.runBlocking(context) {
      block()
      Unit
    }
}
