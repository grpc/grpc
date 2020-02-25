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

import io.grpc.BindableService
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlin.coroutines.CoroutineContext
import kotlin.coroutines.EmptyCoroutineContext

/**
 * Skeleton implementation of a coroutine-based gRPC server implementation.  Intended to be
 * subclassed by generated code.
 */
abstract class AbstractCoroutineServerImpl private constructor(
  private val delegateScope: CoroutineScope
) : CoroutineScope, BindableService {

  constructor(coroutineContext: CoroutineContext = EmptyCoroutineContext) :
    this(CoroutineScope(coroutineContext + SupervisorJob(coroutineContext[Job])))
  // We want a SupervisorJob so one failed RPC does not bring down the entire server.

  final override val coroutineContext: CoroutineContext
    get() = delegateScope.coroutineContext
}
