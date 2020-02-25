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

package io.grpc.examples.routeguide

import com.google.common.base.Stopwatch
import com.google.common.base.Ticker
import com.google.common.io.ByteSource
import com.google.protobuf.util.Durations
import io.grpc.Server
import io.grpc.ServerBuilder
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.flow
import java.util.Collections
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit

/**
 * Kotlin adaptation of RouteGuideServer from the Java gRPC example.
 */
class RouteGuideServer private constructor(
  val port: Int,
  val server: Server
) {
  constructor(port: Int) : this(port, defaultFeatureSource())

  constructor(port: Int, featureData: ByteSource) :
  this(
    serverBuilder = ServerBuilder.forPort(port),
    port = port,
    features = featureData.parseJsonFeatures()
  )

  constructor(
    serverBuilder: ServerBuilder<*>,
    port: Int,
    features: Collection<Feature>
  ) : this(
    port = port,
    server = serverBuilder.addService(RouteGuideService(features)).build()
  )

  fun start() {
    server.start()
    println("Server started, listening on $port")
    Runtime.getRuntime().addShutdownHook(
      Thread {
        println("*** shutting down gRPC server since JVM is shutting down")
        this@RouteGuideServer.stop()
        println("*** server shut down")
      }
    )
  }

  fun stop() {
    server.shutdown()
  }

  fun blockUntilShutdown() {
    server.awaitTermination()
  }

  companion object {
    @JvmStatic
    fun main(args: Array<String>) {
      val port = 8980
      val server = RouteGuideServer(port)
      server.start()
      server.blockUntilShutdown()
    }
  }

  class RouteGuideService(
    val features: Collection<Feature>,
    val ticker: Ticker = Ticker.systemTicker()
  ) : RouteGuideGrpcKt.RouteGuideCoroutineImplBase() {
    private val routeNotes = ConcurrentHashMap<Point, MutableList<RouteNote>>()

    override suspend fun getFeature(request: Point): Feature {
      return features.find { it.location == request }
        ?: Feature.newBuilder().apply { location = request }.build()
      // No feature was found, return an unnamed feature.
    }

    override fun listFeatures(request: Rectangle): Flow<Feature> {
      return features.asFlow().filter { it.exists() && it.location in request }
    }

    override suspend fun recordRoute(requests: Flow<Point>): RouteSummary {
      var pointCount = 0
      var featureCount = 0
      var distance = 0
      var previous: Point? = null
      val stopwatch = Stopwatch.createStarted(ticker)
      requests.collect { request ->
        pointCount++
        if (getFeature(request).exists()) {
          featureCount++
        }
        val prev = previous
        if (prev != null) {
          distance += prev distanceTo request
        }
        previous = request
      }
      return RouteSummary.newBuilder().apply {
        this.pointCount = pointCount
        this.featureCount = featureCount
        this.distance = distance
        this.elapsedTime = Durations.fromMicros(stopwatch.elapsed(TimeUnit.MICROSECONDS))
      }.build()
    }

    override fun routeChat(requests: Flow<RouteNote>): Flow<RouteNote> {
      return flow {
        // could use transform, but it's currently experimental
        requests.collect { note ->
          val notes: MutableList<RouteNote> = routeNotes.computeIfAbsent(note.location) {
            Collections.synchronizedList(mutableListOf<RouteNote>())
          }
          for (prevNote in notes.toTypedArray()) { // thread-safe snapshot
            emit(prevNote)
          }
          notes += note
        }
      }
    }
  }
}
