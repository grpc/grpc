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

import com.google.common.io.MoreFiles
import com.google.common.io.Resources
import com.google.common.jimfs.Jimfs
import com.google.common.truth.Truth.assertThat
import com.google.protobuf.ByteString
import com.google.protobuf.DescriptorProtos.FileDescriptorSet
import com.google.protobuf.compiler.PluginProtos.CodeGeneratorRequest
import com.google.protobuf.compiler.PluginProtos.CodeGeneratorResponse
import io.grpc.examples.helloworld.HelloWorldProto
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.Paths

@RunWith(JUnit4::class)
class GeneratorRunnerTest {
  companion object {
    private const val HELLO_WORLD_DESCRIPTOR_SET_FILE_NAME = "hello-world-descriptor-set.proto.bin"
    private const val OUTPUT_DIR_NAME = "output"
    private val helloWorldDescriptor = HelloWorldProto.getDescriptor().toProto()
    private fun String.removeTrailingWhitespace(): String = lines().joinToString("\n") { it.trimEnd() }
  }

  @Test
  fun runnerCommandLine() {
    val fileSystem = Jimfs.newFileSystem()

    Files.write(
      fileSystem.getPath(HELLO_WORLD_DESCRIPTOR_SET_FILE_NAME),
      FileDescriptorSet.newBuilder()
        .addFile(helloWorldDescriptor)
        .build()
        .toByteArray()
    )

    GeneratorRunner.mainAsCommandLine(
      arrayOf(
        OUTPUT_DIR_NAME,
        HELLO_WORLD_DESCRIPTOR_SET_FILE_NAME,
        "--",
        HELLO_WORLD_DESCRIPTOR_SET_FILE_NAME
      ),
      fileSystem
    )

    val expectedFileContents = Files.readAllLines(
      Paths.get(
        "src/test/java/io/grpc/kotlin/generator",
        "HelloWorldProtoGrpcKt.expected"
      ),
      StandardCharsets.UTF_8
    )

    val outputFile =
      fileSystem
        .getPath(OUTPUT_DIR_NAME, "io/grpc/examples/helloworld")
        .resolve("HelloWorldProtoGrpcKt.kt")
    assertThat(MoreFiles.asCharSource(outputFile, Charsets.UTF_8).read().removeTrailingWhitespace())
      .isEqualTo(expectedFileContents.joinToString("\n") + "\n")
  }

  @Test
  fun runnerProtocPlugin() {
    val output = ByteString.newOutput()
    GeneratorRunner.mainAsProtocPlugin(
      CodeGeneratorRequest.newBuilder()
        .addProtoFile(helloWorldDescriptor)
        .addFileToGenerate(helloWorldDescriptor.name)
        .build()
        .toByteString()
        .newInput(),
      output
    )
    val expectedFileContents = Files.readAllLines(
      Paths.get(
        "src/test/java/io/grpc/kotlin/generator",
        "HelloWorldProtoGrpcKt.expected"
      ),
      StandardCharsets.UTF_8
    )

    val result = CodeGeneratorResponse.parseFrom(output.toByteString())
    assertThat(result.fileList.single().content.removeTrailingWhitespace())
      .isEqualTo(expectedFileContents.joinToString("\n") + "\n")
  }
}
