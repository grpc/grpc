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
import com.google.protobuf.kotlin.protoc.methodName
import com.google.protobuf.kotlin.protoc.of
import com.google.protobuf.kotlin.protoc.serviceName
import com.squareup.kotlinpoet.ClassName
import com.squareup.kotlinpoet.CodeBlock
import com.squareup.kotlinpoet.FunSpec
import com.squareup.kotlinpoet.KModifier
import com.squareup.kotlinpoet.MemberName
import com.squareup.kotlinpoet.MemberName.Companion.member
import com.squareup.kotlinpoet.ParameterSpec
import com.squareup.kotlinpoet.ParameterizedTypeName.Companion.parameterizedBy
import com.squareup.kotlinpoet.TypeSpec
import com.squareup.kotlinpoet.asClassName
import io.grpc.ServerServiceDefinition
import io.grpc.Status
import io.grpc.StatusException
import io.grpc.kotlin.AbstractCoroutineServerImpl
import io.grpc.kotlin.ServerCalls
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.Flow
import kotlin.coroutines.CoroutineContext
import kotlin.coroutines.EmptyCoroutineContext

/**
 * Generator for abstract classes of the form `MyServiceCoroutineImplBase`.
 */
class GrpcCoroutineServerGenerator(config: GeneratorConfig): ServiceCodeGenerator(config) {
  companion object {
    private const val IMPL_BASE_SUFFIX = "CoroutineImplBase"

    private val FLOW: ClassName = Flow::class.asClassName()
    private val UNARY_REQUEST_NAME: MemberSimpleName = MemberSimpleName("request")
    private val STREAMING_REQUEST_NAME: MemberSimpleName = MemberSimpleName("requests")

    private val coroutineContextParameter: ParameterSpec =
      ParameterSpec
        .builder("coroutineContext", CoroutineContext::class)
        .defaultValue("%T", EmptyCoroutineContext::class)
        .build()

    private val SERVER_SERVICE_DEFINITION_BUILDER_FACTORY: MemberName =
      ServerServiceDefinition::class.member("builder")

    private val UNARY_SMD: MemberName = ServerCalls::class.member("unaryServerMethodDefinition")
    private val CLIENT_STREAMING_SMD: MemberName =
      ServerCalls::class.member("clientStreamingServerMethodDefinition")
    private val SERVER_STREAMING_SMD: MemberName =
      ServerCalls::class.member("serverStreamingServerMethodDefinition")
    private val BIDI_STREAMING_SMD: MemberName =
      ServerCalls::class.member("bidiStreamingServerMethodDefinition")

    private val UNIMPLEMENTED_STATUS: MemberName =
      Status::class.member("UNIMPLEMENTED")
  }

  override fun generate(service: ServiceDescriptor): Declarations = declarations {
    addType(implClass(service))
  }

  fun implClass(service: ServiceDescriptor): TypeSpec {
    val serviceImplClassName = service.serviceName.toClassSimpleName().withSuffix(IMPL_BASE_SUFFIX)

    val stubs: List<MethodImplStub> = service.methods.map { serviceMethodStub(it) }
    val implBuilder = TypeSpec
      .classBuilder(serviceImplClassName)
      .addModifiers(KModifier.ABSTRACT)
      .addKdoc(
        """
        Skeletal implementation of the %L service based on Kotlin coroutines.
        """.trimIndent(),
        service.fullName
      )
      .primaryConstructor(
        FunSpec.constructorBuilder()
          .addParameter(coroutineContextParameter)
          .build()
      )
      .superclass(AbstractCoroutineServerImpl::class)
      .addSuperclassConstructorParameter("%N", coroutineContextParameter)

    var serverServiceDefinitionBuilder =
      CodeBlock.of("%M(%M())", SERVER_SERVICE_DEFINITION_BUILDER_FACTORY, service.grpcDescriptor)

    for (stub in stubs) {
      implBuilder.addFunction(stub.methodSpec)
      serverServiceDefinitionBuilder = CodeBlock.of(
        """
          %L
            .addMethod(%L)
        """.trimIndent(),
        serverServiceDefinitionBuilder,
        stub.serverMethodDef
      )
    }

    implBuilder.addFunction(
      FunSpec.builder("bindService")
        .addModifiers(KModifier.OVERRIDE, KModifier.FINAL)
        .returns(ServerServiceDefinition::class)
        .addStatement("return %L.build()", serverServiceDefinitionBuilder)
        .build()
    )

    return implBuilder.build()
  }

  @VisibleForTesting
  data class MethodImplStub(
    val methodSpec: FunSpec,
    /**
     * A [CodeBlock] that computes a [ServerMethodDefinition] based on an implementation of
     * the function described in [methodSpec].
     */
    val serverMethodDef: CodeBlock
  )

  @VisibleForTesting
  fun serviceMethodStub(method: MethodDescriptor): MethodImplStub = with(config) {
    val requestType = method.inputType.messageClass()
    val requestParam = if (method.isClientStreaming) {
      ParameterSpec.of(STREAMING_REQUEST_NAME, FLOW.parameterizedBy(requestType))
    } else {
      ParameterSpec.of(UNARY_REQUEST_NAME, requestType)
    }

    val methodSpecBuilder = FunSpec.builder(method.methodName.toMemberSimpleName())
      .addModifiers(KModifier.OPEN)
      .addParameter(requestParam)
      .addStatement(
        "throw %T(%M.withDescription(%S))",
        StatusException::class,
        UNIMPLEMENTED_STATUS,
        "Method ${method.fullName} is unimplemented"
      )

    val responseType = method.outputType.messageClass()
    if (method.isServerStreaming) {
      methodSpecBuilder.returns(FLOW.parameterizedBy(responseType))
    } else {
      methodSpecBuilder.returns(responseType)
      methodSpecBuilder.addModifiers(KModifier.SUSPEND)
    }

    methodSpecBuilder.addKdoc(stubKDoc(method, requestParam))

    val methodSpec = methodSpecBuilder.build()

    val smdFactory = if (method.isServerStreaming) {
      if (method.isClientStreaming) BIDI_STREAMING_SMD else SERVER_STREAMING_SMD
    } else {
      if (method.isClientStreaming) CLIENT_STREAMING_SMD else UNARY_SMD
    }

    val serverMethodDef =
      CodeBlock.of(
        """
          %M(
            scope = this,
            descriptor = %L,
            implementation = ::%N
          )
        """.trimIndent(),
        smdFactory,
        method.descriptorCode,
        methodSpec
      )

    MethodImplStub(methodSpec, serverMethodDef)
  }

  private fun stubKDoc(
    method: MethodDescriptor,
    requestParam: ParameterSpec
  ): CodeBlock {
    val kDocBindings = mapOf(
      "requestParam" to requestParam,
      "methodName" to method.fullName,
      "flow" to FLOW,
      "status" to Status::class,
      "statusException" to StatusException::class,
      "cancellationException" to CancellationException::class,
      "illegalStateException" to IllegalStateException::class
    )


    val kDocSections = mutableListOf<String>()

    if (method.isServerStreaming) {
      kDocSections.add("Returns a [%flow:T] of responses to an RPC for %methodName:L.")
      kDocSections.add(
        """
          If creating or collecting the returned flow fails with a [%statusException:T], the RPC
          will fail with the corresponding [%status:T].  If it fails with a
          [%cancellationException:T], the RPC will fail with status `Status.CANCELLED`.  If creating
          or collecting the returned flow fails for any other reason, the RPC will fail with
          `Status.UNKNOWN` with the exception as a cause.
        """.trimIndent()
      )
    } else {
      kDocSections.add("Returns the response to an RPC for %methodName:L.")
      kDocSections.add(
        """
          If this method fails with a [%statusException:T], the RPC will fail with the corresponding
          [%status:T].  If this method fails with a [%cancellationException:T], the RPC will fail
          with status `Status.CANCELLED`.  If this method fails for any other reason, the RPC will
          fail with `Status.UNKNOWN` with the exception as a cause.
        """.trimIndent()
      )
    }

    if (method.isClientStreaming) {
      kDocSections.add(
        """
          @param %requestParam:N A [%flow:T] of requests from the client.  This flow can be
                 collected only once and throws [%illegalStateException:T] on attempts to collect
                 it more than once.
        """.trimIndent()
      )
    } else {
      kDocSections.add(
        """
          @param %requestParam:N The request from the client.
        """.trimIndent()
      )
    }

    return CodeBlock
      .builder()
      .addNamed(kDocSections.joinToString("\n\n"), kDocBindings)
      .build()
  }
}
