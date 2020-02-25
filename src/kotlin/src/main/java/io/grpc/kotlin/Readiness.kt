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

import kotlinx.coroutines.channels.Channel

/**
 * A simple helper allowing a notification of "ready" to be broadcast, and waited for.
 */
class Readiness(
  private val isReallyReady: () -> Boolean
) {
  // A CONFLATED channel never suspends to send, and two notifications of readiness are equivalent
  // to one
  private val channel = Channel<Unit>(Channel.CONFLATED)

  fun onReady() {
    if (!channel.offer(Unit)) {
      throw AssertionError(
        "Should be impossible; a CONFLATED channel should never return false on offer"
      )
    }
  }

  suspend fun suspendUntilReady() {
    while (!isReallyReady()) {
      channel.receive()
    }
  }
}
