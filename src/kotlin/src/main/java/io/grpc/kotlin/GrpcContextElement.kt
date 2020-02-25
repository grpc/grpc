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

import kotlinx.coroutines.CoroutineScope
import io.grpc.Context as GrpcContext
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.ThreadContextElement
import kotlinx.coroutines.withContext

/**
 * A [CoroutineContext] that propagates an associated [io.grpc.Context] to coroutines run using
 * that context, regardless of thread.
 */
class GrpcContextElement(private val grpcContext: GrpcContext) : ThreadContextElement<GrpcContext> {
  companion object Key : CoroutineContext.Key<GrpcContextElement> {
    fun current(): GrpcContextElement = GrpcContextElement(GrpcContext.current())
  }

  override val key: CoroutineContext.Key<GrpcContextElement>
    get() = Key

  override fun restoreThreadContext(context: CoroutineContext, oldState: GrpcContext) {
    grpcContext.detach(oldState)
  }

  override fun updateThreadContext(context: CoroutineContext): GrpcContext {
    return grpcContext.attach()
  }
}

/**
 * Runs a coroutine with `this` as the [current][GrpcContext.current] gRPC context, suspends until
 * it completes, and returns the result.
 */
suspend fun <T> GrpcContext.runCoroutine(block: suspend CoroutineScope.() -> T): T =
  withContext(GrpcContextElement(this), block)
