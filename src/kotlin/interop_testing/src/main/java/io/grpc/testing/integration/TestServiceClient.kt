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

import com.google.common.annotations.VisibleForTesting
import com.google.common.io.Files
import io.grpc.ManagedChannel
import io.grpc.ManagedChannelBuilder
import io.grpc.alts.AltsChannelBuilder
import io.grpc.alts.ComputeEngineChannelBuilder
import io.grpc.alts.GoogleDefaultChannelBuilder
import io.grpc.internal.testing.TestUtils
import io.grpc.netty.GrpcSslContexts
import io.grpc.netty.NegotiationType
import io.grpc.netty.NettyChannelBuilder
import io.grpc.okhttp.OkHttpChannelBuilder
import io.grpc.okhttp.internal.Platform
import io.grpc.testing.integration.TestServiceGrpcKt.TestServiceCoroutineStub
import io.netty.handler.ssl.SslContext
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import java.io.File
import java.io.FileInputStream
import java.util.concurrent.TimeUnit
import kotlin.system.exitProcess

/**
 * Application that starts a client for the [TestServiceGrpc.TestServiceImplBase] and runs
 * through a series of tests.
 */
@ExperimentalCoroutinesApi
@FlowPreview
class TestServiceClient {
  private var serverHost = "localhost"
  private var serverHostOverride: String? = null
  private var serverPort = 8080
  private var testCase = "empty_unary"
  private var useTls = true
  private var useAlts = false
  private var useH2cUpgrade = false
  private var customCredentialsType: String? = null
  private var useTestCa = false
  private var useOkHttp = false
  private lateinit var defaultServiceAccount: String
  private lateinit var serviceAccountKeyFile: String
  private lateinit var oauthScope: String
  private var fullStreamDecompression = false
  private val tester = Tester()

  @VisibleForTesting
  fun parseArgs(args: Array<String>) {
    var usage = false
    argsLoop@for (arg in args) {
      if (!arg.startsWith("--")) {
        System.err.println("All arguments must start with '--': $arg")
        usage = true
        break
      }
      val parts = arg.substring(2).split("=".toRegex(), 2).toTypedArray()
      val key = parts[0]
      if ("help" == key) {
        usage = true
        break
      }
      if (parts.size != 2) {
        System.err.println("All arguments must be of the form --arg=value")
        usage = true
        break
      }
      val value = parts[1]
      when (key) {
        "server_host" -> serverHost = value
        "server_host_override" -> serverHostOverride = value
        "server_post" -> serverPort = value.toInt()
        "test_case" -> testCase = value
        "use_tls" -> useTls = value.toBoolean()
        "use_upgrade" -> useH2cUpgrade = value.toBoolean()
        "use_alts" -> useAlts = value.toBoolean()
        "custom_credentials_type" -> customCredentialsType = value
        "use_test_ca" -> useTestCa = value.toBoolean()
        "use_okhttp" -> useOkHttp = value.toBoolean()
        "grpc_version" -> {
          if (value != "2") {
            System.err.println("Only grpc version 2 is supported")
            usage = true
            break@argsLoop
          }
        }
        "default_service_account" -> defaultServiceAccount = value
        "service_account_key_file" -> serviceAccountKeyFile = value
        "oauth_scope" -> oauthScope = value
        "full_stream_decompression" -> fullStreamDecompression = value.toBoolean()
        else -> {
          System.err.println("Unknown argument: $key")
          usage = true
          break@argsLoop
        }
      }
    }
    if (useAlts || useH2cUpgrade) {
      useTls = false
    }
    if (usage) {
      val c = TestServiceClient()
      println(
        """
          |Usage: [ARGS...]
          |
          | --server_host=HOST          Server to connect to. Default ${c.serverHost}
          | --server_host_override=HOST Claimed identification expected of server.
          |                            Defaults to server host
          | --server_port=PORT          Port to connect to. Default ${c.serverPort}
          | --test_case=TESTCASE        Test case to run. Default ${c.testCase}
          |   Valid options:${validTestCasesHelpText()}
          | --use_tls=true|false        Whether to use TLS. Default ${c.useTls}
          | --use_alts=true|false       Whether to use ALTS. Enable ALTS will disable TLS.
          |                             Default ${c.useAlts}
          | --use_upgrade=true|false    Whether to use the h2c Upgrade mechanism.
          |                             Enabling h2c Upgrade will disable TLS.
          |                             Default ${c.useH2cUpgrade}
          | --custom_credentials_type   Custom credentials type to use. Default ${c.customCredentialsType}
          | --use_test_ca=true|false    Whether to trust our fake CA. Requires --use_tls=true 
          |                             to have effect. Default ${c.useTestCa}
          | --use_okhttp=true|false     Whether to use OkHttp instead of Netty. Default ${c.useOkHttp}
          | --default_service_account   Email of GCE default service account. Default ${c.defaultServiceAccount}
          | --service_account_key_file  Path to service account json key file.${c.serviceAccountKeyFile}
          | --oauth_scope               Scope for OAuth tokens. Default ${c.oauthScope}
          | --full_stream_decompression Enable full-stream decompression. Default ${c.fullStreamDecompression}
        """.trimMargin()
      )
      exitProcess(1)
    }
  }

  @VisibleForTesting
  fun setUp() {
    tester.setUp()
  }

  @Synchronized
  private fun tearDown() {
    tester.tearDown()
  }

  private fun run() {
    println("Running test $testCase")
    runTest(TestCases.fromString(testCase))
    println("Test completed.")
  }

  @Throws(Exception::class)
  private fun runTest(testCase: TestCases) {
    when (testCase) {
      TestCases.EMPTY_UNARY -> tester.emptyUnary()
      TestCases.LARGE_UNARY -> tester.largeUnary()
      TestCases.CLIENT_COMPRESSED_UNARY -> tester.clientCompressedUnary(true)
      TestCases.CLIENT_COMPRESSED_UNARY_NOPROBE -> tester.clientCompressedUnary(false)
      TestCases.SERVER_COMPRESSED_UNARY -> tester.serverCompressedUnary()
      TestCases.CLIENT_STREAMING -> tester.clientStreaming()
      TestCases.CLIENT_COMPRESSED_STREAMING -> tester.clientCompressedStreaming(true)
      TestCases.CLIENT_COMPRESSED_STREAMING_NOPROBE -> tester.clientCompressedStreaming(false)
      TestCases.SERVER_STREAMING -> tester.serverStreaming()
      TestCases.SERVER_COMPRESSED_STREAMING -> tester.serverCompressedStreaming()
      TestCases.PING_PONG -> tester.pingPong()
      TestCases.EMPTY_STREAM -> tester.emptyStream()
      TestCases.COMPUTE_ENGINE_CREDS -> tester.computeEngineCreds(defaultServiceAccount, oauthScope)
      TestCases.COMPUTE_ENGINE_CHANNEL_CREDENTIALS -> {
        val channel = ComputeEngineChannelBuilder.forAddress(serverHost, serverPort).build()
        try {
          tester.computeEngineChannelCredentials(
            defaultServiceAccount,
            TestServiceCoroutineStub(channel)
          )
        } finally {
          channel.shutdownNow()
          channel.awaitTermination(5, TimeUnit.SECONDS)
        }
      }
      TestCases.SERVICE_ACCOUNT_CREDS -> {
        val jsonKey = Files.asCharSource(File(serviceAccountKeyFile), UTF_8).read()
        val credentialsStream = FileInputStream(File(serviceAccountKeyFile))
        tester.serviceAccountCreds(jsonKey, credentialsStream, oauthScope)
      }
      TestCases.JWT_TOKEN_CREDS -> {
        val credentialsStream = FileInputStream(File(serviceAccountKeyFile))
        tester.jwtTokenCreds(credentialsStream)
      }
      TestCases.OAUTH2_AUTH_TOKEN -> {
        val jsonKey = Files.asCharSource(File(serviceAccountKeyFile), UTF_8).read()
        val credentialsStream = FileInputStream(File(serviceAccountKeyFile))
        tester.oauth2AuthToken(jsonKey, credentialsStream, oauthScope)
      }
      TestCases.PER_RPC_CREDS -> {
        val jsonKey = Files.asCharSource(File(serviceAccountKeyFile), UTF_8).read()
        val credentialsStream = FileInputStream(File(serviceAccountKeyFile))
        tester.perRpcCreds(jsonKey, credentialsStream, oauthScope)
      }
      TestCases.GOOGLE_DEFAULT_CREDENTIALS -> {
        val channel = GoogleDefaultChannelBuilder.forAddress(serverHost, serverPort).build()
        try {
          val googleDefaultStub = TestServiceCoroutineStub(channel)
          tester.googleDefaultCredentials(defaultServiceAccount, googleDefaultStub)
        } finally {
          channel.shutdownNow()
        }
      }
      TestCases.CUSTOM_METADATA -> tester.customMetadata()
      TestCases.STATUS_CODE_AND_MESSAGE -> tester.statusCodeAndMessage()
      TestCases.SPECIAL_STATUS_MESSAGE -> tester.specialStatusMessage()
      TestCases.UNIMPLEMENTED_METHOD -> tester.unimplementedMethod()
      TestCases.UNIMPLEMENTED_SERVICE -> tester.unimplementedService()
      TestCases.CANCEL_AFTER_BEGIN -> tester.cancelAfterBegin()
      TestCases.CANCEL_AFTER_FIRST_RESPONSE -> tester.cancelAfterFirstResponse()
      TestCases.TIMEOUT_ON_SLEEPING_SERVER -> tester.timeoutOnSleepingServer()
      TestCases.VERY_LARGE_REQUEST -> tester.veryLargeRequest()
      TestCases.PICK_FIRST_UNARY -> tester.pickFirstUnary()
      else -> throw IllegalArgumentException("Unknown test case: $testCase")
    }
  }

  private inner class Tester : AbstractInteropTest() {
    override fun createChannel(): ManagedChannel {
      when (customCredentialsType) {
        "google_default_credentials" -> return GoogleDefaultChannelBuilder.forAddress(serverHost, serverPort).build()
        "compute_engine_channel_creds" -> return ComputeEngineChannelBuilder.forAddress(serverHost, serverPort).build()
      }
      if (useAlts) {
        return AltsChannelBuilder.forAddress(serverHost, serverPort).build()
      }
      val builder: ManagedChannelBuilder<*>
      if (!useOkHttp) {
        var sslContext: SslContext? = null
        if (useTestCa) {
          sslContext =
            GrpcSslContexts.forClient().trustManager(TestUtils.loadCert("ca.pem")).build()
        }
        val nettyBuilder = NettyChannelBuilder.forAddress(serverHost, serverPort)
          .flowControlWindow(65 * 1024)
          .negotiationType(
            when {
              useTls -> NegotiationType.TLS
              useH2cUpgrade -> NegotiationType.PLAINTEXT_UPGRADE
              else -> NegotiationType.PLAINTEXT
            }
          )
          .sslContext(sslContext)
        if (serverHostOverride != null) {
          nettyBuilder.overrideAuthority(serverHostOverride)
        }
        if (fullStreamDecompression) {
          nettyBuilder.enableFullStreamDecompression()
        }
        builder = nettyBuilder
      } else {
        val okBuilder = OkHttpChannelBuilder.forAddress(serverHost, serverPort)
        if (serverHostOverride != null) { // Force the hostname to match the cert the server uses.
          okBuilder.overrideAuthority(
            Util.authorityFromHostAndPort(serverHostOverride, serverPort))
        }
        if (useTls) {
          if (useTestCa) {
            val factory = TestUtils.newSslSocketFactoryForCa(
              Platform.get().provider, TestUtils.loadCert("ca.pem")
            )
            okBuilder.sslSocketFactory(factory)
          }
        } else {
          okBuilder.usePlaintext()
        }
        if (fullStreamDecompression) {
          okBuilder.enableFullStreamDecompression()
        }
        builder = okBuilder
      }
      return builder.build()
    }
  }

  companion object {
    private val UTF_8 = Charsets.UTF_8

    /**
     * The main application allowing this client to be launched from the command line.
     */
    @Throws(Exception::class)
    @JvmStatic
    fun main(args: Array<String>) { // Let Netty or OkHttp use Conscrypt if it is available.
      TestUtils.installConscryptIfAvailable()
      val client = TestServiceClient()
      client.parseArgs(args)
      client.setUp()
      Runtime.getRuntime().addShutdownHook(object : Thread() {
        override fun run() {
          println("Shutting down")
          try {
            client.tearDown()
          } catch (e: Exception) {
            e.printStackTrace()
          }
        }
      })
      try {
        client.run()
      } finally {
        client.tearDown()
      }
      exitProcess(0)
    }

    private fun validTestCasesHelpText(): String {
      val builder = StringBuilder()
      for (testCase in TestCases.values()) {
        val strTestcase = testCase.name.toLowerCase()
        builder.append("\n      ")
          .append(strTestcase)
          .append(": ")
          .append(testCase.description())
      }
      return builder.toString()
    }
  }
}