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
import io.grpc.Context
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.runBlocking
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import java.util.concurrent.Executors

@RunWith(JUnit4::class)
class GrpcContextElementTest {
  val testKey = Context.key<String>("test")

  @Test
  fun testContextPropagation() {
    val testGrpcContext = Context.ROOT.withValue(testKey, "testValue")
    val coroutineContext =
      Executors.newSingleThreadExecutor().asCoroutineDispatcher() + GrpcContextElement(testGrpcContext)
    runBlocking(coroutineContext) {
      val currentTestKey = testKey.get()
      // gets from the implicit current gRPC context
      assertThat(currentTestKey).isEqualTo("testValue")
    }
  }

  @Test
  fun testRun() {
    val testGrpcContext = Context.ROOT.withValue(testKey, "testValue")
    val coroutineContext = Executors.newSingleThreadExecutor().asCoroutineDispatcher()
    runBlocking(coroutineContext) {
      testGrpcContext.runCoroutine {
        assertThat(testKey.get()).isEqualTo("testValue")
      }
    }
  }
}
