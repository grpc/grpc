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

import com.google.protobuf.Descriptors.FileDescriptor
import com.google.protobuf.kotlin.protoc.GeneratorConfig
import com.google.protobuf.kotlin.protoc.builder
import com.google.protobuf.kotlin.protoc.declarations
import com.google.protobuf.kotlin.protoc.objectBuilder
import com.google.protobuf.kotlin.protoc.outerClassSimpleName
import com.google.protobuf.kotlin.protoc.serviceName
import com.squareup.kotlinpoet.FileSpec
import com.squareup.kotlinpoet.TypeSpec

/**
 * Given a list of [ServiceCodeGenerator] factories, generates (optionally) a [FileSpec] of the
 * generated code.
 */
class ProtoFileCodeGenerator(
  generators: List<(GeneratorConfig) -> ServiceCodeGenerator>,
  private val config: GeneratorConfig,
  private val topLevelSuffix: String
) {

  private val generators = generators.map { it(config) }

  fun generateCodeForFile(fileDescriptor: FileDescriptor): FileSpec? = with(config) {
    val outerTypeName = fileDescriptor.outerClassSimpleName.withSuffix(topLevelSuffix)

    var wroteAnything = false
    val fileBuilder = FileSpec.builder(javaPackage(fileDescriptor), outerTypeName)

    for (service in fileDescriptor.services) {
      val serviceDecls = declarations {
        for (generator in generators) {
          merge(generator.generate(service))
        }
      }

      if (serviceDecls.hasEnclosingScopeDeclarations) {
        wroteAnything = true
        val serviceObjectBuilder =
          TypeSpec
            .objectBuilder(service.serviceName.toClassSimpleName().withSuffix(topLevelSuffix))
            .addKdoc(
              """
            Holder for Kotlin coroutine-based client and server APIs for %L.
            """.trimIndent(),
              service.fullName
            )
        serviceDecls.writeToEnclosingType(serviceObjectBuilder)
        fileBuilder.addType(serviceObjectBuilder.build())
      }

      if (serviceDecls.hasTopLevelDeclarations) {
        wroteAnything = true
        serviceDecls.writeOnlyTopLevel(fileBuilder)
      }
    }

    return if (wroteAnything) fileBuilder.build() else null
  }
}
