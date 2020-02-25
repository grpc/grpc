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

import com.google.auth.oauth2.ComputeEngineCredentials
import com.google.auth.oauth2.GoogleCredentials
import com.google.auth.oauth2.OAuth2Credentials
import com.google.auth.oauth2.ServiceAccountCredentials
import com.google.common.annotations.VisibleForTesting
import com.google.common.collect.ImmutableList
import com.google.common.truth.Truth.assertThat
import com.google.protobuf.ByteString
import com.google.protobuf.MessageLite
import io.grpc.CallOptions
import io.grpc.Channel
import io.grpc.ClientCall
import io.grpc.ClientInterceptor
import io.grpc.ClientStreamTracer
import io.grpc.Context
import io.grpc.Grpc
import io.grpc.ManagedChannel
import io.grpc.Metadata
import io.grpc.MethodDescriptor
import io.grpc.Server
import io.grpc.ServerBuilder
import io.grpc.ServerCall
import io.grpc.ServerCallHandler
import io.grpc.ServerInterceptor
import io.grpc.ServerInterceptors
import io.grpc.ServerStreamTracer
import io.grpc.Status
import io.grpc.StatusException
import io.grpc.StatusRuntimeException
import io.grpc.auth.MoreCallCredentials
import io.grpc.internal.testing.TestClientStreamTracer
import io.grpc.internal.testing.TestServerStreamTracer
import io.grpc.internal.testing.TestStreamTracer
import io.grpc.stub.MetadataUtils
import io.grpc.testing.TestUtils
import io.grpc.testing.integration.Messages.BoolValue
import io.grpc.testing.integration.Messages.EchoStatus
import io.grpc.testing.integration.Messages.Payload
import io.grpc.testing.integration.Messages.ResponseParameters
import io.grpc.testing.integration.Messages.SimpleContext
import io.grpc.testing.integration.Messages.SimpleRequest
import io.grpc.testing.integration.Messages.SimpleResponse
import io.grpc.testing.integration.Messages.StreamingInputCallRequest
import io.grpc.testing.integration.Messages.StreamingInputCallResponse
import io.grpc.testing.integration.Messages.StreamingOutputCallRequest
import io.grpc.testing.integration.Messages.StreamingOutputCallResponse
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.channels.toList
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.consumeAsFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.produceIn
import kotlinx.coroutines.flow.single
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.DisableOnDebug
import org.junit.rules.TestRule
import org.junit.rules.Timeout
import java.io.IOException
import java.io.InputStream
import java.net.SocketAddress
import java.util.Arrays
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.Executors
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import java.util.logging.Level
import java.util.logging.Logger
import java.util.regex.Pattern
import kotlin.math.max
import kotlin.test.assertFailsWith

/**
 * Abstract base class for all GRPC transport tests.
 *
 *
 *  New tests should avoid using Mockito to support running on AppEngine.
 */
@ExperimentalCoroutinesApi
@FlowPreview
abstract class AbstractInteropTest {
  @get:Rule
  val globalTimeout: TestRule
  private val serverCallCapture = AtomicReference<ServerCall<*, *>>()
  private val clientCallCapture = AtomicReference<ClientCall<*, *>>()
  private val requestHeadersCapture = AtomicReference<Metadata?>()
  private val contextCapture = AtomicReference<Context>()
  private lateinit var testServiceExecutor: ScheduledExecutorService
  private var server: Server? = null
  private val serverStreamTracers = LinkedBlockingQueue<ServerStreamTracerInfo>()

  private class ServerStreamTracerInfo internal constructor(
    val fullMethodName: String,
    val tracer: InteropServerStreamTracer
  ) {

    class InteropServerStreamTracer : TestServerStreamTracer() {
      @Volatile
      var contextCapture: Context? = null

      override fun filterContext(context: Context): Context {
        contextCapture = context
        return super.filterContext(context)
      }
    }
  }

  private val serverStreamTracerFactory: ServerStreamTracer.Factory =
    object : ServerStreamTracer.Factory() {
      override fun newServerStreamTracer(
        fullMethodName: String,
        headers: Metadata
      ): ServerStreamTracer {
        val tracer = ServerStreamTracerInfo.InteropServerStreamTracer()
        serverStreamTracers.add(ServerStreamTracerInfo(fullMethodName, tracer))
        return tracer
      }
    }

  private fun startServer() {
    val builder = serverBuilder
    if (builder == null) {
      server = null
      return
    }
    testServiceExecutor = Executors.newScheduledThreadPool(2)
    val allInterceptors: List<ServerInterceptor> = ImmutableList.builder<ServerInterceptor>()
      .add(recordServerCallInterceptor(serverCallCapture))
      .add(TestUtils.recordRequestHeadersInterceptor(requestHeadersCapture))
      .add(recordContextInterceptor(contextCapture))
      .addAll(TestServiceImpl.interceptors)
      .build()
    builder
      .addService(
        ServerInterceptors.intercept(
          TestServiceImpl(testServiceExecutor),
          allInterceptors
        )
      )
      .addStreamTracerFactory(serverStreamTracerFactory)
    server = try {
      builder.build().start()
    } catch (ex: IOException) {
      throw RuntimeException(ex)
    }
  }

  private fun stopServer() {
    server?.shutdownNow()
    testServiceExecutor.shutdown()
  }

  @get:VisibleForTesting
  val listenAddress: SocketAddress
    get() = server!!.listenSockets.first()

  protected lateinit var channel: ManagedChannel

  protected lateinit var stub: TestServiceGrpcKt.TestServiceCoroutineStub

  // to be deleted when subclasses are ready to migrate
  @JvmField
  var blockingStub: TestServiceGrpc.TestServiceBlockingStub? = null

  // to be deleted when subclasses are ready to migrate
  @JvmField
  var asyncStub: TestServiceGrpc.TestServiceStub? = null

  private val clientStreamTracers = LinkedBlockingQueue<TestClientStreamTracer>()
  private val clientStreamTracerFactory: ClientStreamTracer.Factory =
    object : ClientStreamTracer.Factory() {
      override fun newClientStreamTracer(
        info: ClientStreamTracer.StreamInfo,
        headers: Metadata
      ): ClientStreamTracer {
        val tracer = TestClientStreamTracer()
        clientStreamTracers.add(tracer)
        return tracer
      }
    }
  private val tracerSetupInterceptor: ClientInterceptor = object : ClientInterceptor {
    override fun <ReqT, RespT> interceptCall(
      method: MethodDescriptor<ReqT, RespT>,
      callOptions: CallOptions,
      next: Channel
    ): ClientCall<ReqT, RespT> {
      return next.newCall(
        method, callOptions.withStreamTracerFactory(clientStreamTracerFactory)
      )
    }
  }

  /**
   * Must be called by the subclass setup method if overridden.
   */
  @Before
  fun setUp() {
    startServer()
    channel = createChannel()
    stub =
      TestServiceGrpcKt.TestServiceCoroutineStub(channel).withInterceptors(tracerSetupInterceptor)
    blockingStub = TestServiceGrpc.newBlockingStub(channel).withInterceptors(tracerSetupInterceptor)
    asyncStub = TestServiceGrpc.newStub(channel).withInterceptors(tracerSetupInterceptor)
    val additionalInterceptors = additionalInterceptors
    if (additionalInterceptors != null) {
      stub = stub.withInterceptors(*additionalInterceptors)
    }
    requestHeadersCapture.set(null)
  }

  /** Clean up.  */
  @After
  open fun tearDown() {
    channel.shutdownNow()
    try {
      channel.awaitTermination(1, TimeUnit.SECONDS)
    } catch (ie: InterruptedException) {
      logger.log(Level.FINE, "Interrupted while waiting for channel termination", ie)
      // Best effort. If there is an interruption, we want to continue cleaning up, but quickly
      Thread.currentThread().interrupt()
    }
    stopServer()
  }

  protected abstract fun createChannel(): ManagedChannel

  protected val additionalInterceptors: Array<ClientInterceptor>?
    get() = null

  /**
   * Returns the server builder used to create server for each test run.  Return `null` if
   * it shouldn't start a server in the same process.
   */
  protected open val serverBuilder: ServerBuilder<*>?
    get() = null

  @Test
  fun emptyUnary() {
    runBlocking {
      assertEquals(EMPTY, stub.emptyCall(EMPTY))
    }
  }

  @Test
  fun largeUnary() {
    assumeEnoughMemory()
    val request = SimpleRequest.newBuilder()
      .setResponseSize(314159)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(271828)))
      )
      .build()
    val goldenResponse = SimpleResponse.newBuilder()
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(314159)))
      )
      .build()
    runBlocking {
      assertResponse(goldenResponse, stub.unaryCall(request))
    }
    assertStatsTrace(
      "grpc.testing.TestService/UnaryCall",
      Status.Code.OK,
      setOf(request),
      setOf(goldenResponse)
    )
  }

  /**
   * Tests client per-message compression for unary calls. The Java API does not support inspecting
   * a message's compression level, so this is primarily intended to run against a gRPC C++ server.
   */
  fun clientCompressedUnary(probe: Boolean) {
    assumeEnoughMemory()
    val expectCompressedRequest = SimpleRequest.newBuilder()
      .setExpectCompressed(BoolValue.newBuilder().setValue(true))
      .setResponseSize(314159)
      .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(271828))))
      .build()
    val expectUncompressedRequest = SimpleRequest.newBuilder()
      .setExpectCompressed(BoolValue.newBuilder().setValue(false))
      .setResponseSize(314159)
      .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(271828))))
      .build()
    val goldenResponse = SimpleResponse.newBuilder()
      .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(314159))))
      .build()
    if (probe) {
      // Send a non-compressed message with expectCompress=true. Servers supporting this test case
      // should return INVALID_ARGUMENT.
      runBlocking {
        try {
          stub.unaryCall(expectCompressedRequest)
          fail("expected INVALID_ARGUMENT")
        } catch (e: StatusRuntimeException) {
          assertEquals(Status.INVALID_ARGUMENT.code, e.status.code)
        }
      }
      assertStatsTrace("grpc.testing.TestService/UnaryCall", Status.Code.INVALID_ARGUMENT)
    }
    runBlocking {
      assertResponse(
        goldenResponse, stub.withCompression("gzip").unaryCall(expectCompressedRequest)
      )
      assertStatsTrace(
        "grpc.testing.TestService/UnaryCall",
        Status.Code.OK, setOf(expectCompressedRequest), setOf(goldenResponse)
      )
      assertResponse(goldenResponse, stub.unaryCall(expectUncompressedRequest))
      assertStatsTrace(
        "grpc.testing.TestService/UnaryCall",
        Status.Code.OK, setOf(expectUncompressedRequest), setOf(goldenResponse)
      )
    }
  }

  /**
   * Tests if the server can send a compressed unary response. Ideally we would assert that the
   * responses have the requested compression, but this is not supported by the API. Given a
   * compliant server, this test will exercise the code path for receiving a compressed response but
   * cannot itself verify that the response was compressed.
   */
  @Test
  fun serverCompressedUnary() {
    assumeEnoughMemory()
    val responseShouldBeCompressed = SimpleRequest.newBuilder()
      .setResponseCompressed(BoolValue.newBuilder().setValue(true))
      .setResponseSize(314159)
      .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(271828))))
      .build()
    val responseShouldBeUncompressed = SimpleRequest.newBuilder()
      .setResponseCompressed(BoolValue.newBuilder().setValue(false))
      .setResponseSize(314159)
      .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(271828))))
      .build()
    val goldenResponse = SimpleResponse.newBuilder()
      .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(314159))))
      .build()
    runBlocking {
      assertResponse(goldenResponse, stub.unaryCall(responseShouldBeCompressed))
      assertStatsTrace(
        "grpc.testing.TestService/UnaryCall",
        Status.Code.OK, setOf(responseShouldBeCompressed), setOf(goldenResponse)
      )
      assertResponse(goldenResponse, stub.unaryCall(responseShouldBeUncompressed))
      assertStatsTrace(
        "grpc.testing.TestService/UnaryCall",
        Status.Code.OK, setOf(responseShouldBeUncompressed), setOf(goldenResponse)
      )
    }
  }

  /**
   * Assuming "pick_first" policy is used, tests that all requests are sent to the same server.
   */
  fun pickFirstUnary() {
    val request = SimpleRequest.newBuilder()
      .setResponseSize(1)
      .setFillServerId(true)
      .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(1))))
      .build()
    runBlocking {
      val firstResponse = stub.unaryCall(request)
      // Increase the chance of all servers are connected, in case the channel should be doing
      // round_robin instead.
      delay(5000)
      for (i in 0..99) {
        val response = stub.unaryCall(request)
        assertThat(response.serverId).isEqualTo(firstResponse.serverId)
      }
    }
  }

  @Test
  fun serverStreaming() {
    val request = StreamingOutputCallRequest.newBuilder()
      .addResponseParameters(
        ResponseParameters.newBuilder()
          .setSize(31415)
      )
      .addResponseParameters(
        ResponseParameters.newBuilder()
          .setSize(9)
      )
      .addResponseParameters(
        ResponseParameters.newBuilder()
          .setSize(2653)
      )
      .addResponseParameters(
        ResponseParameters.newBuilder()
          .setSize(58979)
      )
      .build()
    val goldenResponses = Arrays.asList(
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(31415)))
        )
        .build(),
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(9)))
        )
        .build(),
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(2653)))
        )
        .build(),
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(58979)))
        )
        .build()
    )
    runBlocking {
      assertResponses(goldenResponses, stub.streamingOutputCall(request).toList())
    }
  }

  @Test
  fun clientStreaming() {
    val requests = Arrays.asList(
      StreamingInputCallRequest.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(27182)))
        )
        .build(),
      StreamingInputCallRequest.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(8)))
        )
        .build(),
      StreamingInputCallRequest.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(1828)))
        )
        .build(),
      StreamingInputCallRequest.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(45904)))
        )
        .build()
    )
    val goldenResponse = StreamingInputCallResponse.newBuilder()
      .setAggregatedPayloadSize(74922)
      .build()
    val response = runBlocking {
      stub.streamingInputCall(requests.asFlow())
    }
    assertEquals(goldenResponse, response)
  }

  /**
   * Unsupported.
   */
  open fun clientCompressedStreaming(probe: Boolean) {
    val expectCompressedRequest = StreamingInputCallRequest.newBuilder()
      .setExpectCompressed(BoolValue.newBuilder().setValue(true))
      .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(27182))))
      .build()

    if (probe) {
      runBlocking {
        val ex = assertFailsWith<StatusException> {
          // Send a non-compressed message with expectCompress=true. Servers supporting this test
          // case should return INVALID_ARGUMENT.
          stub.streamingInputCall(flowOf(expectCompressedRequest))
        }
        assertEquals(Status.INVALID_ARGUMENT.code, ex.status.code)
      }
    }
    // the Kotlin API doesn't expose control over compression
  }

  /**
   * Tests server per-message compression in a streaming response. Ideally we would assert that the
   * responses have the requested compression, but this is not supported by the API. Given a
   * compliant server, this test will exercise the code path for receiving a compressed response but
   * cannot itself verify that the response was compressed.
   */
  fun serverCompressedStreaming() {
    val request = StreamingOutputCallRequest.newBuilder()
      .addResponseParameters(
        ResponseParameters.newBuilder()
          .setCompressed(BoolValue.newBuilder().setValue(true))
          .setSize(31415)
      )
      .addResponseParameters(
        ResponseParameters.newBuilder()
          .setCompressed(BoolValue.newBuilder().setValue(false))
          .setSize(92653)
      )
      .build()
    val goldenResponses = Arrays.asList(
      StreamingOutputCallResponse.newBuilder()
        .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(31415))))
        .build(),
      StreamingOutputCallResponse.newBuilder()
        .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(92653))))
        .build()
    )
    runBlocking {
      assertResponses(goldenResponses, stub.streamingOutputCall(request).toList())
    }
  }

  @Test
  fun pingPong() {
    val requests = Arrays.asList(
      StreamingOutputCallRequest.newBuilder()
        .addResponseParameters(
          ResponseParameters.newBuilder()
            .setSize(31415)
        )
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(27182)))
        )
        .build(),
      StreamingOutputCallRequest.newBuilder()
        .addResponseParameters(
          ResponseParameters.newBuilder()
            .setSize(9)
        )
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(8)))
        )
        .build(),
      StreamingOutputCallRequest.newBuilder()
        .addResponseParameters(
          ResponseParameters.newBuilder()
            .setSize(2653)
        )
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(1828)))
        )
        .build(),
      StreamingOutputCallRequest.newBuilder()
        .addResponseParameters(
          ResponseParameters.newBuilder()
            .setSize(58979)
        )
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(45904)))
        )
        .build()
    )
    val goldenResponses = listOf(
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(31415)))
        )
        .build(),
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(9)))
        )
        .build(),
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(2653)))
        )
        .build(),
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(58979)))
        )
        .build()
    )
    runBlocking {
      // TODO: per-element timeout
      assertResponses(goldenResponses, stub.fullDuplexCall(requests.asFlow()).toList())
    }
  }

  @Test
  fun emptyStream() {
    runBlocking {
      assertResponses(listOf(), stub.fullDuplexCall(flowOf()).toList())
    }
  }

  @Test
  fun cancelAfterBegin() {
    class MyEx : Exception()
    runBlocking {
      assertFailsWith<MyEx> {
        stub.streamingInputCall(
          flow {
            throw MyEx()
          }
        )
      }
    }
  }

  @Test
  fun cancelAfterFirstResponse() {
    val request = StreamingOutputCallRequest.newBuilder()
      .addResponseParameters(
        ResponseParameters.newBuilder()
          .setSize(31415)
      )
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(27182)))
      )
      .build()

    runBlocking {
      val ex = assertFailsWith<StatusException> {
        stub
          .fullDuplexCall(
            flow {
              emit(request)
              throw CancellationException()
            }
          )
          .collect()
      }
      assertThat(ex.status.code).isEqualTo(Status.Code.CANCELLED)
    }
    assertStatsTrace("grpc.testing.TestService/FullDuplexCall", Status.Code.CANCELLED)
  }

  @Test
  fun fullDuplexCallShouldSucceed() { // Build the request.
    val responseSizes = listOf(50, 100, 150, 200)
    val streamingOutputBuilder = StreamingOutputCallRequest.newBuilder()
    for (size in responseSizes) {
      streamingOutputBuilder.addResponseParameters(
        ResponseParameters.newBuilder().setSize(size).setIntervalUs(0)
      )
    }
    val request = streamingOutputBuilder.build()
    val numRequests = 10
    val responses = runBlocking {
      stub.fullDuplexCall(
        (1..numRequests).asFlow().map { request }
      ).toList()
    }
    assertEquals(responseSizes.size * numRequests, responses.size)
    for ((ix, response) in responses.withIndex()) {
      val length = response.payload.body.size()
      val expectedSize = responseSizes[ix % responseSizes.size]
      assertEquals("comparison failed at index $ix", expectedSize.toLong(), length.toLong())
    }
    assertStatsTrace(
      "grpc.testing.TestService/FullDuplexCall",
      Status.Code.OK,
      List(numRequests) { request },
      responses
    )
  }

  @Test
  fun halfDuplexCallShouldSucceed() { // Build the request.
    val responseSizes = listOf(50, 100, 150, 200)
    val streamingOutputBuilder = StreamingOutputCallRequest.newBuilder()
    for (size in responseSizes) {
      streamingOutputBuilder.addResponseParameters(
        ResponseParameters.newBuilder().setSize(size).setIntervalUs(0)
      )
    }
    val request = streamingOutputBuilder.build()

    val numRequests = 10
    val responses = runBlocking {
      stub.halfDuplexCall((1..numRequests).asFlow().map { request }).toList()
    }
    assertEquals(responseSizes.size * numRequests, responses.size)
    for ((ix, response) in responses.withIndex()) {
      val length = response.payload.body.size()
      val expectedSize = responseSizes[ix % responseSizes.size]
      assertEquals("comparison failed at index $ix", expectedSize.toLong(), length.toLong())
    }
  }

  @Test
  fun serverStreamingShouldBeFlowControlled() {
    val request = StreamingOutputCallRequest.newBuilder()
      .addResponseParameters(ResponseParameters.newBuilder().setSize(100000))
      .addResponseParameters(ResponseParameters.newBuilder().setSize(100001))
      .build()
    val goldenResponses = Arrays.asList(
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(100000)))
        ).build(),
      StreamingOutputCallResponse.newBuilder()
        .setPayload(
          Payload.newBuilder()
            .setBody(ByteString.copyFrom(ByteArray(100001)))
        ).build()
    )
    val start = System.nanoTime()

    // TODO(lowasser): change this to a Channel
    val queue = ArrayBlockingQueue<Any>(10)
    val call = channel.newCall(TestServiceGrpc.getStreamingOutputCallMethod(), CallOptions.DEFAULT)
    call.start(
      object : ClientCall.Listener<StreamingOutputCallResponse>() {
        override fun onHeaders(headers: Metadata) {}
        override fun onMessage(message: StreamingOutputCallResponse) {
          queue.add(message)
        }

        override fun onClose(status: Status, trailers: Metadata) {
          queue.add(status)
        }
      },
      Metadata()
    )
    call.sendMessage(request)
    call.halfClose()
    // Time how long it takes to get the first response.
    call.request(1)
    var response = queue.poll(operationTimeoutMillis().toLong(), TimeUnit.MILLISECONDS)
    assertTrue(response is StreamingOutputCallResponse)
    assertResponse(goldenResponses[0], response as StreamingOutputCallResponse)
    val firstCallDuration = System.nanoTime() - start
    // Without giving additional flow control, make sure that we don't get another response. We wait
    // until we are comfortable the next message isn't coming. We may have very low nanoTime
    // resolution (like on Windows) or be using a testing, in-process transport where message
    // handling is instantaneous. In both cases, firstCallDuration may be 0, so round up sleep time
    // to at least 1ms.
    assertNull(
      queue.poll(max(firstCallDuration * 4, 1 * 1000 * 1000.toLong()), TimeUnit.NANOSECONDS)
    )
    // Make sure that everything still completes.
    call.request(1)
    response = queue.poll(operationTimeoutMillis().toLong(), TimeUnit.MILLISECONDS)
    assertTrue(response is StreamingOutputCallResponse)
    assertResponse(goldenResponses[1], response as StreamingOutputCallResponse)
    assertEquals(Status.OK, queue.poll(operationTimeoutMillis().toLong(), TimeUnit.MILLISECONDS))
  }

  @Test
  fun veryLargeRequest() {
    assumeEnoughMemory()
    val request = SimpleRequest.newBuilder()
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(unaryPayloadLength())))
      )
      .setResponseSize(10)
      .build()
    val goldenResponse = SimpleResponse.newBuilder()
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(10)))
      )
      .build()
    runBlocking {
      assertResponse(goldenResponse, stub.unaryCall(request))
    }
  }

  @Test
  fun veryLargeResponse() {
    assumeEnoughMemory()
    val request = SimpleRequest.newBuilder()
      .setResponseSize(unaryPayloadLength())
      .build()
    val goldenResponse = SimpleResponse.newBuilder()
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(unaryPayloadLength())))
      )
      .build()
    runBlocking {
      assertResponse(goldenResponse, stub.unaryCall(request))
    }
  }

  @Test
  fun exchangeMetadataUnaryCall() {
    // Capture the metadata exchange
    val fixedHeaders = Metadata()
    // Send a context proto (as it's in the default extension registry)
    val contextValue = SimpleContext.newBuilder().setValue("dog").build()
    fixedHeaders.put(Util.METADATA_KEY, contextValue)
    stub = MetadataUtils.attachHeaders(stub, fixedHeaders)
    // .. and expect it to be echoed back in trailers
    val trailersCapture = AtomicReference<Metadata>()
    val headersCapture = AtomicReference<Metadata>()
    stub = MetadataUtils.captureMetadata(stub, headersCapture, trailersCapture)
    runBlocking {
      assertNotNull(stub.emptyCall(EMPTY))
    }
    // Assert that our side channel object is echoed back in both headers and trailers
    assertEquals(contextValue, headersCapture.get().get(Util.METADATA_KEY))
    assertEquals(contextValue, trailersCapture.get().get(Util.METADATA_KEY))
  }

  @Test
  fun exchangeMetadataStreamingCall() {
    // Capture the metadata exchange
    val fixedHeaders = Metadata()
    // Send a context proto (as it's in the default extension registry)
    val contextValue = SimpleContext.newBuilder().setValue("dog").build()
    fixedHeaders.put(Util.METADATA_KEY, contextValue)
    stub = MetadataUtils.attachHeaders(stub, fixedHeaders)
    // .. and expect it to be echoed back in trailers
    val trailersCapture = AtomicReference<Metadata>()
    val headersCapture = AtomicReference<Metadata>()
    stub = MetadataUtils.captureMetadata(stub, headersCapture, trailersCapture)
    val responseSizes = listOf(50, 100, 150, 200)
    val streamingOutputBuilder = StreamingOutputCallRequest.newBuilder()
    for (size in responseSizes) {
      streamingOutputBuilder.addResponseParameters(
        ResponseParameters.newBuilder().setSize(size).setIntervalUs(0)
      )
    }
    val request = streamingOutputBuilder.build()

    val numRequests = 10
    val responses = runBlocking {
      stub.fullDuplexCall((1..numRequests).asFlow().map { request }).toList()
    }
    assertEquals(responseSizes.size * numRequests, responses.size)
    // Assert that our side channel object is echoed back in both headers and trailers
    assertEquals(contextValue, headersCapture.get().get(Util.METADATA_KEY))
    assertEquals(contextValue, trailersCapture.get().get(Util.METADATA_KEY))
  }

  @Test
  fun deadlineNotExceeded() {
    runBlocking {
      // warm up the channel and JVM
      stub.emptyCall(EmptyProtos.Empty.getDefaultInstance())
      stub
        .withDeadlineAfter(10, TimeUnit.SECONDS)
        .streamingOutputCall(
          StreamingOutputCallRequest.newBuilder()
            .addResponseParameters(
              ResponseParameters.newBuilder()
                .setIntervalUs(0)
            )
            .build()
        )
        .first()
    }
  }

  @Test
  fun deadlineExceeded() {
    runBlocking {
      // warm up the channel and JVM
      stub.emptyCall(EmptyProtos.Empty.getDefaultInstance())
      val request = StreamingOutputCallRequest.newBuilder()
        .addResponseParameters(
          ResponseParameters.newBuilder()
            .setIntervalUs(TimeUnit.SECONDS.toMicros(20).toInt())
        )
        .build()
      try {
        stub.withDeadlineAfter(100, TimeUnit.MILLISECONDS).streamingOutputCall(request).first()
        fail("Expected deadline to be exceeded")
      } catch (ex: StatusException) {
        assertEquals(Status.DEADLINE_EXCEEDED.code, ex.status.code)
        val desc = ex.status.description
        assertTrue(
          desc, // There is a race between client and server-side deadline expiration.
          // If client expires first, it'd generate this message
          Pattern.matches("deadline exceeded after .*s. \\[.*\\]", desc!!) ||
            // If server expires first, it'd reset the stream and client would generate a different
            // message
            desc.startsWith("ClientCall was cancelled at or after deadline.")
        )
      }
      assertStatsTrace("grpc.testing.TestService/EmptyCall", Status.Code.OK)
    }
  }

  @Test
  fun deadlineExceededServerStreaming() {
    runBlocking {
      // warm up the channel and JVM
      stub.emptyCall(EmptyProtos.Empty.getDefaultInstance())
      val responseParameters = ResponseParameters.newBuilder()
        .setSize(1)
        .setIntervalUs(10000)
      val request = StreamingOutputCallRequest.newBuilder()
        .addResponseParameters(responseParameters)
        .addResponseParameters(responseParameters)
        .addResponseParameters(responseParameters)
        .addResponseParameters(responseParameters)
        .build()
      val statusEx = assertFailsWith<StatusException> {
        stub
          .withDeadlineAfter(30, TimeUnit.MILLISECONDS)
          .streamingOutputCall(request)
          .collect()
      }
      assertEquals(Status.DEADLINE_EXCEEDED.code, statusEx.status.code)
      assertStatsTrace("grpc.testing.TestService/EmptyCall", Status.Code.OK)
    }
  }

  @Test
  fun deadlineInPast() { // Test once with idle channel and once with active channel
    runBlocking {
      try {
        stub
          .withDeadlineAfter(-10, TimeUnit.SECONDS)
          .emptyCall(EmptyProtos.Empty.getDefaultInstance())
        fail("Should have thrown")
      } catch (ex: StatusException) {
        assertEquals(Status.Code.DEADLINE_EXCEEDED, ex.status.code)
        assertThat(ex.status.description)
          .startsWith("ClientCall started after deadline exceeded")
      }
      // warm up the channel
      stub.emptyCall(EmptyProtos.Empty.getDefaultInstance())
      try {
        stub
          .withDeadlineAfter(-10, TimeUnit.SECONDS)
          .emptyCall(EmptyProtos.Empty.getDefaultInstance())
        fail("Should have thrown")
      } catch (ex: StatusException) {
        assertEquals(Status.Code.DEADLINE_EXCEEDED, ex.status.code)
        assertThat(ex.status.description)
          .startsWith("ClientCall started after deadline exceeded")
      }
      assertStatsTrace("grpc.testing.TestService/EmptyCall", Status.Code.OK)
    }
  }

  protected fun unaryPayloadLength(): Int { // 10MiB.
    return 10485760
  }

  @Test
  fun gracefulShutdown() {
    runBlocking {
      val requests = listOf(
        StreamingOutputCallRequest.newBuilder()
          .addResponseParameters(
            ResponseParameters.newBuilder()
              .setSize(3)
          )
          .setPayload(
            Payload.newBuilder()
              .setBody(ByteString.copyFrom(ByteArray(2)))
          )
          .build(),
        StreamingOutputCallRequest.newBuilder()
          .addResponseParameters(
            ResponseParameters.newBuilder()
              .setSize(1)
          )
          .setPayload(
            Payload.newBuilder()
              .setBody(ByteString.copyFrom(ByteArray(7)))
          )
          .build(),
        StreamingOutputCallRequest.newBuilder()
          .addResponseParameters(
            ResponseParameters.newBuilder()
              .setSize(4)
          )
          .setPayload(
            Payload.newBuilder()
              .setBody(ByteString.copyFrom(ByteArray(1)))
          )
          .build()
      )
      val goldenResponses = listOf(
        StreamingOutputCallResponse.newBuilder()
          .setPayload(
            Payload.newBuilder()
              .setBody(ByteString.copyFrom(ByteArray(3)))
          )
          .build(),
        StreamingOutputCallResponse.newBuilder()
          .setPayload(
            Payload.newBuilder()
              .setBody(ByteString.copyFrom(ByteArray(1)))
          )
          .build(),
        StreamingOutputCallResponse.newBuilder()
          .setPayload(
            Payload.newBuilder()
              .setBody(ByteString.copyFrom(ByteArray(4)))
          )
          .build()
      )

      val requestChannel = kotlinx.coroutines.channels.Channel<StreamingOutputCallRequest>()

      val responses = stub.fullDuplexCall(requestChannel.consumeAsFlow()).produceIn(this)

      requestChannel.send(requests[0])
      assertResponse(goldenResponses[0], responses.receive())
      // Initiate graceful shutdown.
      channel.shutdown()
      requestChannel.send(requests[1])
      assertResponse(goldenResponses[1], responses.receive())
      // The previous ping-pong could have raced with the shutdown, but this one certainly shouldn't.
      requestChannel.send(requests[2])
      assertResponse(goldenResponses[2], responses.receive())
      assertFalse(responses.isClosedForReceive)
      requestChannel.close()
      assertThat(responses.toList()).isEmpty()
    }
  }

  @Test
  fun customMetadata() {
    val responseSize = 314159
    val requestSize = 271828
    val request = SimpleRequest.newBuilder()
      .setResponseSize(responseSize)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(requestSize)))
      )
      .build()
    val streamingRequest = StreamingOutputCallRequest.newBuilder()
      .addResponseParameters(ResponseParameters.newBuilder().setSize(responseSize))
      .setPayload(Payload.newBuilder().setBody(ByteString.copyFrom(ByteArray(requestSize))))
      .build()
    val goldenResponse = SimpleResponse.newBuilder()
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(responseSize)))
      )
      .build()
    val goldenStreamingResponse = StreamingOutputCallResponse.newBuilder()
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(responseSize)))
      )
      .build()
    val trailingBytes =
      byteArrayOf(
        0xa.toByte(), 0xb.toByte(), 0xa.toByte(), 0xb.toByte(), 0xa.toByte(), 0xb.toByte()
      )
    // Test UnaryCall
    var metadata = Metadata()
    metadata.put(Util.ECHO_INITIAL_METADATA_KEY, "test_initial_metadata_value")
    metadata.put(Util.ECHO_TRAILING_METADATA_KEY, trailingBytes)
    var theStub = stub
    theStub = MetadataUtils.attachHeaders(theStub, metadata)
    var headersCapture = AtomicReference<Metadata>()
    var trailersCapture = AtomicReference<Metadata>()
    theStub = MetadataUtils.captureMetadata(theStub, headersCapture, trailersCapture)
    val response = runBlocking { theStub.unaryCall(request) }
    assertResponse(goldenResponse, response)
    assertEquals(
      "test_initial_metadata_value",
      headersCapture.get().get(Util.ECHO_INITIAL_METADATA_KEY)
    )
    assertTrue(
      Arrays.equals(trailingBytes, trailersCapture.get().get(Util.ECHO_TRAILING_METADATA_KEY))
    )
    assertStatsTrace(
      "grpc.testing.TestService/UnaryCall", Status.Code.OK, setOf(request), setOf(goldenResponse)
    )
    // Test FullDuplexCall
    metadata = Metadata()
    metadata.put(Util.ECHO_INITIAL_METADATA_KEY, "test_initial_metadata_value")
    metadata.put(Util.ECHO_TRAILING_METADATA_KEY, trailingBytes)

    var theStreamingStub = stub
    theStreamingStub = MetadataUtils.attachHeaders(theStreamingStub, metadata)
    headersCapture = AtomicReference()
    trailersCapture = AtomicReference()
    theStreamingStub = MetadataUtils.captureMetadata(theStreamingStub, headersCapture, trailersCapture)
    runBlocking {
      assertResponse(goldenStreamingResponse, theStreamingStub.fullDuplexCall(flowOf(streamingRequest)).single())
    }
    assertEquals(
      "test_initial_metadata_value",
      headersCapture.get().get(Util.ECHO_INITIAL_METADATA_KEY)
    )
    assertTrue(
      Arrays.equals(trailingBytes, trailersCapture.get().get(Util.ECHO_TRAILING_METADATA_KEY))
    )
    assertStatsTrace(
      "grpc.testing.TestService/FullDuplexCall",
      Status.Code.OK,
      setOf(streamingRequest),
      setOf(goldenStreamingResponse)
    )
  }

  @Test
  fun statusCodeAndMessage() {
    runBlocking {
      val errorCode = 2
      val errorMessage = "test status message"
      val responseStatus = EchoStatus.newBuilder()
        .setCode(errorCode)
        .setMessage(errorMessage)
        .build()
      val simpleRequest = SimpleRequest.newBuilder()
        .setResponseStatus(responseStatus)
        .build()
      val streamingRequest = StreamingOutputCallRequest.newBuilder()
        .setResponseStatus(responseStatus)
        .build()
      // Test UnaryCall
      try {
        stub.unaryCall(simpleRequest)
        fail()
      } catch (e: StatusException) {
        assertEquals(Status.UNKNOWN.code, e.status.code)
        assertEquals(errorMessage, e.status.description)
      }
      assertStatsTrace("grpc.testing.TestService/UnaryCall", Status.Code.UNKNOWN)
      // Test FullDuplexCall
      val status = assertFailsWith<StatusException> {
        stub.fullDuplexCall(flowOf(streamingRequest)).collect()
      }.status
      assertEquals(Status.UNKNOWN.code, status.code)
      assertEquals(errorMessage, status.description)
      assertStatsTrace("grpc.testing.TestService/FullDuplexCall", Status.Code.UNKNOWN)
    }
  }

  @Test
  fun specialStatusMessage() {
    val errorCode = 2
    val errorMessage = "\t\ntest with whitespace\r\nand Unicode BMP ☺ and non-BMP 😈\t\n"
    val simpleRequest = SimpleRequest.newBuilder()
      .setResponseStatus(
        EchoStatus.newBuilder()
          .setCode(errorCode)
          .setMessage(errorMessage)
          .build()
      )
      .build()
    runBlocking {
      try {
        stub.unaryCall(simpleRequest)
        fail()
      } catch (e: StatusException) {
        assertEquals(Status.UNKNOWN.code, e.status.code)
        assertEquals(errorMessage, e.status.description)
      }
    }
    assertStatsTrace("grpc.testing.TestService/UnaryCall", Status.Code.UNKNOWN)
  }

  /** Sends an rpc to an unimplemented method within TestService.  */
  @Test
  fun unimplementedMethod() {
    runBlocking {
      val ex = assertFailsWith<StatusException> {
        stub.unimplementedCall(EmptyProtos.Empty.getDefaultInstance())
      }
      assertEquals(Status.UNIMPLEMENTED.code, ex.status.code)
      assertClientStatsTrace(
        "grpc.testing.TestService/UnimplementedCall",
        Status.Code.UNIMPLEMENTED
      )
    }
  }

  /** Sends an rpc to an unimplemented service on the server.  */
  @Test
  fun unimplementedService() {
    val stub =
      UnimplementedServiceGrpcKt.UnimplementedServiceCoroutineStub(channel)
        .withInterceptors(tracerSetupInterceptor)
    runBlocking {
      val ex = assertFailsWith<StatusException> {
        stub.unimplementedCall(EmptyProtos.Empty.getDefaultInstance())
      }
      assertEquals(Status.UNIMPLEMENTED.code, ex.status.code)
    }
    assertStatsTrace(
      "grpc.testing.UnimplementedService/UnimplementedCall",
      Status.Code.UNIMPLEMENTED
    )
  }

  /** Start a fullDuplexCall which the server will not respond, and verify the deadline expires.  */
  @Test
  fun timeoutOnSleepingServer() {
    val stub = stub.withDeadlineAfter(1, TimeUnit.MILLISECONDS)
    val request = StreamingOutputCallRequest.newBuilder()
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(27182)))
      )
      .build()
    runBlocking {
      val caught = CompletableDeferred<Throwable>()
      val responses = stub.fullDuplexCall(flowOf(request)).catch { caught.complete(it) }.toList()
      assertThat(responses).isEmpty()
      assertEquals(Status.DEADLINE_EXCEEDED.code, (caught.getCompleted() as StatusException).status.code)
    }
  }

  /**
   * Verifies remote server address and local client address are available from ClientCall
   * Attributes via ClientInterceptor.
   */
  @Test
  fun getServerAddressAndLocalAddressFromClient() {
    assertNotNull(obtainRemoteServerAddr())
    assertNotNull(obtainLocalClientAddr())
  }

  /** Sends a large unary rpc with service account credentials.  */
  fun serviceAccountCreds(jsonKey: String, credentialsStream: InputStream?, authScope: String) {
    // cast to ServiceAccountCredentials to double-check the right type of object was created.
    var credentials: GoogleCredentials =
      GoogleCredentials.fromStream(credentialsStream) as ServiceAccountCredentials
    credentials = credentials.createScoped(listOf(authScope))
    val stub = this.stub.withCallCredentials(MoreCallCredentials.from(credentials))
    val request = SimpleRequest.newBuilder()
      .setFillUsername(true)
      .setFillOauthScope(true)
      .setResponseSize(314159)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(271828)))
      )
      .build()
    val response = runBlocking { stub.unaryCall(request) }
    assertFalse(response.username.isEmpty())
    assertTrue(
      "Received username: " + response.username,
      jsonKey.contains(response.username)
    )
    assertFalse(response.oauthScope.isEmpty())
    assertTrue(
      "Received oauth scope: " + response.oauthScope,
      authScope.contains(response.oauthScope)
    )
    val goldenResponse = SimpleResponse.newBuilder()
      .setOauthScope(response.oauthScope)
      .setUsername(response.username)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(314159)))
      )
      .build()
    assertResponse(goldenResponse, response)
  }

  /** Sends a large unary rpc with compute engine credentials.  */
  fun computeEngineCreds(serviceAccount: String?, oauthScope: String) {
    val credentials = ComputeEngineCredentials.create()
    val stub = stub.withCallCredentials(MoreCallCredentials.from(credentials))
    val request = SimpleRequest.newBuilder()
      .setFillUsername(true)
      .setFillOauthScope(true)
      .setResponseSize(314159)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(271828)))
      )
      .build()
    val response = runBlocking { stub.unaryCall(request) }
    assertEquals(serviceAccount, response.username)
    assertFalse(response.oauthScope.isEmpty())
    assertTrue(
      "Received oauth scope: " + response.oauthScope,
      oauthScope.contains(response.oauthScope)
    )
    val goldenResponse = SimpleResponse.newBuilder()
      .setOauthScope(response.oauthScope)
      .setUsername(response.username)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(314159)))
      )
      .build()
    assertResponse(goldenResponse, response)
  }

  /** Sends an unary rpc with ComputeEngineChannelBuilder.  */
  fun computeEngineChannelCredentials(
    defaultServiceAccount: String,
    computeEngineStub: TestServiceGrpcKt.TestServiceCoroutineStub
  ) = runBlocking {
    val request = SimpleRequest.newBuilder()
      .setFillUsername(true)
      .setResponseSize(314159)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(271828)))
      )
      .build()
    val response = computeEngineStub.unaryCall(request)
    assertEquals(defaultServiceAccount, response.username)
    val goldenResponse = SimpleResponse.newBuilder()
      .setUsername(defaultServiceAccount)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(314159)))
      )
      .build()
    assertResponse(goldenResponse, response)
  }

  /** Test JWT-based auth.  */
  fun jwtTokenCreds(serviceAccountJson: InputStream?) {
    val request = SimpleRequest.newBuilder()
      .setResponseSize(314159)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(271828)))
      )
      .setFillUsername(true)
      .build()
    val credentials = GoogleCredentials.fromStream(serviceAccountJson) as ServiceAccountCredentials
    val response = runBlocking {
      stub.withCallCredentials(MoreCallCredentials.from(credentials)).unaryCall(request)
    }
    assertEquals(credentials.clientEmail, response.username)
    assertEquals(314159, response.payload.body.size().toLong())
  }

  /** Sends a unary rpc with raw oauth2 access token credentials.  */
  fun oauth2AuthToken(jsonKey: String, credentialsStream: InputStream, authScope: String) {
    var utilCredentials = GoogleCredentials.fromStream(credentialsStream)
    utilCredentials = utilCredentials.createScoped(listOf(authScope))
    val accessToken = utilCredentials.refreshAccessToken()
    val credentials = OAuth2Credentials.create(accessToken)
    val request = SimpleRequest.newBuilder()
      .setFillUsername(true)
      .setFillOauthScope(true)
      .build()
    val response = runBlocking {
      stub.withCallCredentials(MoreCallCredentials.from(credentials)).unaryCall(request)
    }
    assertFalse(response.username.isEmpty())
    assertTrue(
      "Received username: " + response.username,
      jsonKey.contains(response.username)
    )
    assertFalse(response.oauthScope.isEmpty())
    assertTrue(
      "Received oauth scope: " + response.oauthScope,
      authScope.contains(response.oauthScope)
    )
  }

  /** Sends a unary rpc with "per rpc" raw oauth2 access token credentials.  */
  fun perRpcCreds(jsonKey: String, credentialsStream: InputStream, oauthScope: String) {
    // In gRpc Java, we don't have per Rpc credentials, user can use an intercepted stub only once
    // for that purpose.
    // So, this test is identical to oauth2_auth_token test.
    oauth2AuthToken(jsonKey, credentialsStream, oauthScope)
  }

  /** Sends an unary rpc with "google default credentials".  */
  fun googleDefaultCredentials(
    defaultServiceAccount: String,
    googleDefaultStub: TestServiceGrpcKt.TestServiceCoroutineStub
  ) = runBlocking {
    val request = SimpleRequest.newBuilder()
      .setFillUsername(true)
      .setResponseSize(314159)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(271828)))
      )
      .build()
    val response = googleDefaultStub.unaryCall(request)
    assertEquals(defaultServiceAccount, response.username)
    val goldenResponse = SimpleResponse.newBuilder()
      .setUsername(defaultServiceAccount)
      .setPayload(
        Payload.newBuilder()
          .setBody(ByteString.copyFrom(ByteArray(314159)))
      )
      .build()
    assertResponse(goldenResponse, response)
  }

  /** Helper for getting remote address from [io.grpc.ClientCall.getAttributes]  */
  private fun obtainRemoteServerAddr(): SocketAddress? {
    return runBlocking {
      stub
        .withInterceptors(recordClientCallInterceptor(clientCallCapture))
        .withDeadlineAfter(5, TimeUnit.SECONDS)
        .unaryCall(SimpleRequest.getDefaultInstance())
      clientCallCapture.get().attributes.get(Grpc.TRANSPORT_ATTR_REMOTE_ADDR)
    }
  }

  /** Helper for getting local address from [io.grpc.ClientCall.getAttributes]  */
  private fun obtainLocalClientAddr(): SocketAddress? {
    return runBlocking {
      stub
        .withInterceptors(recordClientCallInterceptor(clientCallCapture))
        .withDeadlineAfter(5, TimeUnit.SECONDS)
        .unaryCall(SimpleRequest.getDefaultInstance())
      clientCallCapture.get().attributes.get(Grpc.TRANSPORT_ATTR_LOCAL_ADDR)
    }
  }

  protected fun operationTimeoutMillis(): Int {
    return 5000
  }

  /**
   * Poll the next metrics record and check it against the provided information, including the
   * message sizes.
   */
  /**
   * Poll the next metrics record and check it against the provided information, without checking
   * the message sizes.
   */
  private fun assertStatsTrace(
    method: String,
    status: Status.Code,
    requests: Collection<MessageLite>? = null,
    responses: Collection<MessageLite>? = null
  ) {
    assertClientStatsTrace(method, status, requests, responses)
    assertServerStatsTrace(method, status, requests, responses)
  }

  private fun assertClientStatsTrace(
    @Suppress("UNUSED_PARAMETER") method: String,
    code: Status.Code,
    requests: Collection<MessageLite>?,
    responses: Collection<MessageLite>?
  ) { // Tracer-based stats
    val tracer: TestClientStreamTracer? = clientStreamTracers.poll()
    assertTrue(tracer!!.outboundHeaders)
    // assertClientStatsTrace() is called right after application receives status,
    // but streamClosed() may be called slightly later than that.  So we need a timeout.
    try {
      assertTrue(tracer.await(5, TimeUnit.SECONDS))
    } catch (e: InterruptedException) {
      throw AssertionError(e)
    }
    assertEquals(code, tracer.status.code)
    if (requests != null && responses != null) {
      checkTracers(tracer, requests, responses)
    }
  }

  private fun assertClientStatsTrace(method: String, status: Status.Code) {
    assertClientStatsTrace(method, status, null, null)
  }

  // Failure is checked in the end by the passed flag.
  private fun assertServerStatsTrace(
    method: String,
    code: Status.Code,
    requests: Collection<MessageLite>?,
    responses: Collection<MessageLite>?
  ) {
    if (server == null) { // Server is not in the same process.  We can't check server-side stats.
      return
    }
    val tracerInfo: ServerStreamTracerInfo? = serverStreamTracers.poll()
    assertEquals(method, tracerInfo!!.fullMethodName)
    assertNotNull(tracerInfo.tracer.contextCapture)
    // On the server, streamClosed() may be called after the client receives the final status.
// So we use a timeout.
    try {
      assertTrue(tracerInfo.tracer.await(1, TimeUnit.SECONDS))
    } catch (e: InterruptedException) {
      throw AssertionError(e)
    }
    assertEquals(code, tracerInfo.tracer.status.code)
    if (requests != null && responses != null) {
      checkTracers(tracerInfo.tracer, responses, requests)
    }
  }

  /**
   * Check information recorded by tracers.
   */
  private fun checkTracers(
    tracer: TestStreamTracer,
    sentMessages: Collection<MessageLite>,
    receivedMessages: Collection<MessageLite>
  ) {
    var uncompressedSentSize: Long = 0
    var seqNo = 0
    for (msg in sentMessages) {
      assertThat(tracer.nextOutboundEvent()).isEqualTo(String.format("outboundMessage(%d)", seqNo))
      assertThat(tracer.nextOutboundEvent()).matches(
        String.format("outboundMessageSent\\(%d, -?[0-9]+, -?[0-9]+\\)", seqNo)
      )
      seqNo++
      uncompressedSentSize += msg.serializedSize.toLong()
    }
    assertNull(tracer.nextOutboundEvent())
    var uncompressedReceivedSize: Long = 0
    seqNo = 0
    for (msg in receivedMessages) {
      assertThat(tracer.nextInboundEvent()).isEqualTo(String.format("inboundMessage(%d)", seqNo))
      assertThat(tracer.nextInboundEvent()).matches(
        String.format("inboundMessageRead\\(%d, -?[0-9]+, -?[0-9]+\\)", seqNo)
      )
      uncompressedReceivedSize += msg.serializedSize.toLong()
      seqNo++
    }
    assertNull(tracer.nextInboundEvent())
  }

  // Helper methods for responses containing Payload since proto equals does not ignore deprecated
  // fields (PayloadType).
  private fun assertResponses(
    expected: Collection<StreamingOutputCallResponse>,
    actual: Collection<StreamingOutputCallResponse>
  ) {
    assertSame(expected.size, actual.size)
    val expectedIter = expected.iterator()
    val actualIter = actual.iterator()
    while (expectedIter.hasNext()) {
      assertResponse(expectedIter.next(), actualIter.next())
    }
  }

  private fun assertResponse(
    expected: StreamingOutputCallResponse?,
    actual: StreamingOutputCallResponse?
  ) {
    if (expected == null || actual == null) {
      assertEquals(expected, actual)
    } else {
      assertPayload(expected.payload, actual.payload)
    }
  }

  private fun assertResponse(expected: SimpleResponse, actual: SimpleResponse) {
    assertPayload(expected.payload, actual.payload)
    assertEquals(expected.username, actual.username)
    assertEquals(expected.oauthScope, actual.oauthScope)
  }

  private fun assertPayload(expected: Payload?, actual: Payload?) {
    // Compare non deprecated fields in Payload, to make this test forward compatible.
    if (expected == null || actual == null) {
      assertEquals(expected, actual)
    } else {
      assertEquals(expected.body, actual.body)
    }
  }

  companion object {
    private val logger = Logger.getLogger(AbstractInteropTest::class.java.name)
    /** Must be at least [.unaryPayloadLength], plus some to account for encoding overhead.  */
    const val MAX_MESSAGE_SIZE = 16 * 1024 * 1024
    @JvmField
    protected val EMPTY = EmptyProtos.Empty.getDefaultInstance()

    /**
     * Some tests run on memory constrained environments.  Rather than OOM, just give up.  64 is
     * chosen as a maximum amount of memory a large test would need.
     */
    private fun assumeEnoughMemory() {
      val r = Runtime.getRuntime()
      val usedMem = r.totalMemory() - r.freeMemory()
      val actuallyFreeMemory = r.maxMemory() - usedMem
      Assume.assumeTrue(
        "$actuallyFreeMemory is not sufficient to run this test",
        actuallyFreeMemory >= 64 * 1024 * 1024
      )
    }

    /**
     * Captures the request attributes. Useful for testing ServerCalls.
     * [ServerCall.getAttributes]
     */
    private fun recordServerCallInterceptor(
      serverCallCapture: AtomicReference<ServerCall<*, *>>
    ): ServerInterceptor {
      return object : ServerInterceptor {
        override fun <ReqT, RespT> interceptCall(
          call: ServerCall<ReqT, RespT>,
          requestHeaders: Metadata,
          next: ServerCallHandler<ReqT, RespT>
        ): ServerCall.Listener<ReqT> {
          serverCallCapture.set(call)
          return next.startCall(call, requestHeaders)
        }
      }
    }

    /**
     * Captures the request attributes. Useful for testing ClientCalls.
     * [ClientCall.getAttributes]
     */
    private fun recordClientCallInterceptor(
      clientCallCapture: AtomicReference<ClientCall<*, *>>
    ): ClientInterceptor {
      return object : ClientInterceptor {
        override fun <ReqT, RespT> interceptCall(
          method: MethodDescriptor<ReqT, RespT>,
          callOptions: CallOptions,
          next: Channel
        ): ClientCall<ReqT, RespT> =
          next.newCall(method, callOptions).also { clientCallCapture.set(it) }
      }
    }

    private fun recordContextInterceptor(
      contextCapture: AtomicReference<Context>
    ): ServerInterceptor {
      return object : ServerInterceptor {
        override fun <ReqT, RespT> interceptCall(
          call: ServerCall<ReqT, RespT>,
          requestHeaders: Metadata,
          next: ServerCallHandler<ReqT, RespT>
        ): ServerCall.Listener<ReqT> {
          contextCapture.set(Context.current())
          return next.startCall(call, requestHeaders)
        }
      }
    }
  }

  /**
   * Constructor for tests.
   */
  init {
    var timeout: TestRule = Timeout.seconds(60)
    try {
      timeout = DisableOnDebug(timeout)
    } catch (t: Throwable) { // This can happen on Android, which lacks some standard Java class.
      // Seen at https://github.com/grpc/grpc-java/pull/5832#issuecomment-499698086
      logger.log(Level.FINE, "Debugging not disabled.", t)
    }
    globalTimeout = timeout
  }
}
