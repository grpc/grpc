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

import com.google.common.annotations.VisibleForTesting
import com.google.protobuf.Descriptors.MethodDescriptor
import com.google.protobuf.Descriptors.ServiceDescriptor
import com.google.protobuf.kotlin.protoc.Declarations
import com.google.protobuf.kotlin.protoc.GeneratorConfig
import com.google.protobuf.kotlin.protoc.MemberSimpleName
import com.google.protobuf.kotlin.protoc.builder
import com.google.protobuf.kotlin.protoc.classBuilder
import com.google.protobuf.kotlin.protoc.declarations
import com.google.protobuf.kotlin.protoc.member
import com.google.protobuf.kotlin.protoc.methodName
import com.google.protobuf.kotlin.protoc.of
import com.google.protobuf.kotlin.protoc.serviceName
import com.squareup.kotlinpoet.AnnotationSpec
import com.squareup.kotlinpoet.CodeBlock
import com.squareup.kotlinpoet.FunSpec
import com.squareup.kotlinpoet.KModifier
import com.squareup.kotlinpoet.ParameterSpec
import com.squareup.kotlinpoet.ParameterizedTypeName.Companion.parameterizedBy
import com.squareup.kotlinpoet.TypeName
import com.squareup.kotlinpoet.TypeSpec
import com.squareup.kotlinpoet.TypeVariableName
import com.squareup.kotlinpoet.asClassName
import com.squareup.kotlinpoet.asTypeName
import io.grpc.CallOptions
import io.grpc.Channel as GrpcChannel
import io.grpc.Metadata as GrpcMetadata
import io.grpc.MethodDescriptor.MethodType
import io.grpc.Status
import io.grpc.StatusException
import io.grpc.kotlin.AbstractCoroutineStub
import io.grpc.kotlin.ClientCalls
import io.grpc.kotlin.StubFor
import kotlinx.coroutines.flow.Flow

/**
 * Logic for generating gRPC stubs for Kotlin.
 */
@VisibleForTesting
class GrpcClientStubGenerator(config: GeneratorConfig) : ServiceCodeGenerator(config) {
  companion object {
    private const val STUB_CLASS_SUFFIX = "CoroutineStub"
    private val UNARY_PARAMETER_NAME = MemberSimpleName("request")
    private val STREAMING_PARAMETER_NAME = MemberSimpleName("requests")
    private val GRPC_CHANNEL_PARAMETER_NAME = MemberSimpleName("channel")
    private val CALL_OPTIONS_PARAMETER_NAME = MemberSimpleName("callOptions")

    private val HEADERS_PARAMETER: ParameterSpec = ParameterSpec
      .builder("headers", GrpcMetadata::class)
      .defaultValue("%T()", GrpcMetadata::class)
      .build()

    val GRPC_CHANNEL_PARAMETER = ParameterSpec.of(GRPC_CHANNEL_PARAMETER_NAME, GrpcChannel::class)
    val CALL_OPTIONS_PARAMETER = ParameterSpec
      .builder(MemberSimpleName("callOptions"), CallOptions::class)
      .defaultValue("%M", CallOptions::class.member("DEFAULT"))
      .build()

    private val FLOW = Flow::class.asClassName()

    private val UNARY_RPC_HELPER = ClientCalls::class.member("unaryRpc")
    private val CLIENT_STREAMING_RPC_HELPER = ClientCalls::class.member("clientStreamingRpc")
    private val SERVER_STREAMING_RPC_HELPER = ClientCalls::class.member("serverStreamingRpc")
    private val BIDI_STREAMING_RPC_HELPER = ClientCalls::class.member("bidiStreamingRpc")

    private val RPC_HELPER = mapOf(
      MethodType.UNARY to UNARY_RPC_HELPER,
      MethodType.CLIENT_STREAMING to CLIENT_STREAMING_RPC_HELPER,
      MethodType.SERVER_STREAMING to SERVER_STREAMING_RPC_HELPER,
      MethodType.BIDI_STREAMING to BIDI_STREAMING_RPC_HELPER
    )

    private val MethodDescriptor.type: MethodType
      get() = if (isClientStreaming) {
        if (isServerStreaming) MethodType.BIDI_STREAMING else MethodType.CLIENT_STREAMING
      } else {
        if (isServerStreaming) MethodType.SERVER_STREAMING else MethodType.UNARY
      }
  }

  override fun generate(service: ServiceDescriptor): Declarations = declarations {
    addType(generateStub(service))
  }

  @VisibleForTesting
  fun generateStub(service: ServiceDescriptor): TypeSpec {
    val stubName = service.serviceName.toClassSimpleName().withSuffix(STUB_CLASS_SUFFIX)

    // Not actually a TypeVariableName, but this at least prevents the name from being imported,
    // which we don't want.
    val stubSelfReference: TypeName = TypeVariableName(stubName.toString())

    val builder = TypeSpec
      .classBuilder(stubName)
      .superclass(AbstractCoroutineStub::class.asTypeName().parameterizedBy(stubSelfReference))
      .addKdoc(
        "A stub for issuing RPCs to a(n) %L service as suspending coroutines.",
        service.fullName
      )
      .addAnnotation(
        AnnotationSpec.builder(StubFor::class)
          .addMember("%T::class", service.grpcClass)
          .build()
      )
      .primaryConstructor(
        FunSpec
          .constructorBuilder()
          .addParameter(GRPC_CHANNEL_PARAMETER)
          .addParameter(CALL_OPTIONS_PARAMETER)
          .addAnnotation(JvmOverloads::class)
          .build()
      )
      .addSuperclassConstructorParameter("%N", GRPC_CHANNEL_PARAMETER)
      .addSuperclassConstructorParameter("%N", CALL_OPTIONS_PARAMETER)
      .addFunction(buildFun(stubSelfReference))

    for (method in service.methods) {
      builder.addFunction(generateRpcStub(method))
    }
    return builder.build()
  }

  /**
   * Outputs a `FunSpec` of an override of `AbstractCoroutineStub.build` for this particular stub.
   */
  private fun buildFun(stubName: TypeName): FunSpec {
    return FunSpec
      .builder("build")
      .returns(stubName)
      .addModifiers(KModifier.OVERRIDE)
      .addParameter(GRPC_CHANNEL_PARAMETER)
      .addParameter(ParameterSpec.of(CALL_OPTIONS_PARAMETER_NAME, CallOptions::class))
      .addStatement(
        "return %T(%N, %N)",
        stubName,
        GRPC_CHANNEL_PARAMETER,
        CALL_OPTIONS_PARAMETER
      )
      .build()
  }

  @VisibleForTesting
  fun generateRpcStub(method: MethodDescriptor): FunSpec = with(config) {
    val name = method.methodName.toMemberSimpleName()
    val requestType = method.inputType.messageClass()
    val parameter = if (method.isClientStreaming) {
      ParameterSpec.of(STREAMING_PARAMETER_NAME, FLOW.parameterizedBy(requestType))
    } else {
      ParameterSpec.of(UNARY_PARAMETER_NAME, requestType)
    }

    val responseType = method.outputType.messageClass()

    val returnType =
      if (method.isServerStreaming) FLOW.parameterizedBy(responseType) else responseType

    val helperMethod = RPC_HELPER[method.type] ?: throw IllegalArgumentException()

    val funSpecBuilder =
      funSpecBuilder(name)
        .addParameter(parameter)
        .addParameter(HEADERS_PARAMETER)
        .returns(returnType)
        .addKdoc(rpcStubKDoc(method, parameter))

    val codeBlockMap = mapOf(
      "helperMethod" to helperMethod,
      "methodDescriptor" to method.descriptorCode,
      "parameter" to parameter,
      "headers" to HEADERS_PARAMETER
    )

    if (!method.isServerStreaming) {
      funSpecBuilder.addModifiers(KModifier.SUSPEND)
    }

    funSpecBuilder.addNamedCode(
      """
      return %helperMethod:M(
        channel,
        %methodDescriptor:L,
        %parameter:N,
        callOptions,
        %headers:N
      )
      """.trimIndent(),
      codeBlockMap
    )
    return funSpecBuilder.build()
  }

  private fun rpcStubKDoc(
    method: MethodDescriptor,
    parameter: ParameterSpec
  ): CodeBlock {
    val kDocBindings = mapOf(
      "parameter" to parameter,
      "flow" to Flow::class,
      "status" to Status::class,
      "statusException" to StatusException::class
    )

    val kDocComponents = mutableListOf<String>()

    kDocComponents.add(
      if (method.isServerStreaming) {
        """
        Returns a [%flow:T] that, when collected, executes this RPC and emits responses from the
        server as they arrive.  That flow finishes normally if the server closes its response with
        [`Status.OK`][%status:T], and fails by throwing a [%statusException:T] otherwise.  If
        collecting the flow downstream fails exceptionally (including via cancellation), the RPC
        is cancelled with that exception as a cause.
        """.trimIndent()
      } else {
        """
        Executes this RPC and returns the response message, suspending until the RPC completes
        with [`Status.OK`][%status:T].  If the RPC completes with another status, a corresponding
        [%statusException:T] is thrown.  If this coroutine is cancelled, the RPC is also cancelled
        with the corresponding exception as a cause.
        """.trimIndent()
      }
    )

    when (method.type) {
      MethodType.BIDI_STREAMING -> {
        kDocComponents.add(
          """
          The [%flow:T] of requests is collected once each time the [%flow:T] of responses is
          collected. If collection of the [%flow:T] of responses completes normally or
          exceptionally before collection of `%parameter:N` completes, the collection of
          `%parameter:N` is cancelled.  If the collection of `%parameter:N` completes
          exceptionally for any other reason, then the collection of the [%flow:T] of responses
          completes exceptionally for the same reason and the RPC is cancelled with that reason.
          """.trimIndent()
        )
      }
      MethodType.CLIENT_STREAMING -> {
        kDocComponents.add(
          """
          This function collects the [%flow:T] of requests.  If the server terminates the RPC
          for any reason before collection of requests is complete, the collection of requests
          will be cancelled.  If the collection of requests completes exceptionally for any other
          reason, the RPC will be cancelled for that reason and this method will throw that
          exception.
          """.trimIndent()
        )
      }
      else -> {}
    }

    kDocComponents.add(
      if (method.isClientStreaming) {
        "@param %parameter:N A [%flow:T] of request messages."
      } else {
        "@param %parameter:N The request message to send to the server."
      }
    )

    kDocComponents.add(
      if (method.isServerStreaming) {
        "@return A flow that, when collected, emits the responses from the server."
      } else {
        "@return The single response from the server."
      }
    )
    return CodeBlock
      .builder()
      .addNamed(kDocComponents.joinToString("\n\n"), kDocBindings)
      .build()
  }
}
