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
import io.grpc.ServerServiceDefinition
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.debug.junit4.CoroutinesTimeout
import kotlinx.coroutines.runBlocking
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import kotlin.coroutines.CoroutineContext

@RunWith(JUnit4::class)
class AbstractCoroutineServerImplTest {
  @get:Rule
  val timeout = CoroutinesTimeout.seconds(10)

  class MyException: Exception()

  @Test
  fun oneFailureDoesNotBreakServer() = runBlocking {
    val caughtException = CompletableDeferred<Throwable>()
    val exceptionHandler = object: CoroutineExceptionHandler {
      override val key: CoroutineContext.Key<*>
        get() = CoroutineExceptionHandler

      override fun handleException(context: CoroutineContext, exception: Throwable) {
        caughtException.complete(exception)
      }
    }

    val waitingResult = CompletableDeferred<Int>()

    val serverImpl = object: AbstractCoroutineServerImpl(exceptionHandler) {
      override fun bindService(): ServerServiceDefinition = throw UnsupportedOperationException()

      fun throwExceptionInScope() = async<Int> {
        throw MyException()
      }

      fun waitForResult() = async {
        waitingResult.await()
      }
    }

    val waitResult = serverImpl.waitForResult()
    // We now have a sub-Job in the AbstractCoroutineServerImpl's CoroutineScope
    try {
      serverImpl.throwExceptionInScope().await()
      fail("Expected MyException")
    } catch (expected: MyException) {}
    waitingResult.complete(15)
    assertThat(waitResult.await()).isEqualTo(15)
  }

  @Test
  fun cancelServerScope() = runBlocking {
    val waitingResult = CompletableDeferred<Int>()

    val job = Job()
    val serverImpl = object: AbstractCoroutineServerImpl(job) {
      override fun bindService(): ServerServiceDefinition = throw UnsupportedOperationException()

      fun waitForResult() = async {
        waitingResult.await()
      }
    }

    val waitResult = serverImpl.waitForResult()
    job.cancel()
    waitResult.join()
    assertThat(waitResult.isCancelled).isTrue()
  }
}
