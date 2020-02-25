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

import io.grpc.ManagedChannel
import io.grpc.ManagedChannelBuilder
import io.grpc.examples.routeguide.RouteGuideGrpcKt.RouteGuideCoroutineStub
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.asExecutor
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import java.io.Closeable
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.random.Random
import kotlin.random.nextLong

class RouteGuideClient private constructor(
  val channel: ManagedChannel,
  val stub: RouteGuideCoroutineStub,
  val printer: Printer
) : Closeable {
  private val random = Random(314159)

  constructor(
    channelBuilder: ManagedChannelBuilder<*>,
    dispatcher: CoroutineDispatcher,
    printer: Printer
  ) : this(channelBuilder.executor(dispatcher.asExecutor()).build(), printer)

  constructor(
    channel: ManagedChannel,
    printer: Printer
  ) : this(channel, RouteGuideCoroutineStub(channel), printer)

  companion object {
    @JvmStatic
    fun main(args: Array<String>) {
      val features = defaultFeatureSource().parseJsonFeatures()
      Executors.newFixedThreadPool(10).asCoroutineDispatcher().use { dispatcher ->
        RouteGuideClient(
          ManagedChannelBuilder.forAddress("localhost", 8980)
            .usePlaintext(),
          dispatcher,
          Printer.stdout
        ).use {
          it.getFeature(409146138, -746188906)
          it.getFeature(0, 0)
          it.listFeatures(400000000, -750000000, 420000000, -730000000)
          it.recordRoute(features, 10)
          it.routeChat()
        }
      }
    }

    private fun point(lat: Int, lon: Int): Point =
      Point.newBuilder()
        .setLatitude(lat)
        .setLongitude(lon)
        .build()
  }

  override fun close() {
    channel.shutdown().awaitTermination(5, TimeUnit.SECONDS)
  }

  interface Printer {
    companion object {
      val stdout = object : Printer {
        override fun println(str: String) {
          System.out.println(str)
        }
      }
    }
    fun println(str: String)
  }

  fun getFeature(latitude: Int, longitude: Int) = runBlocking {
    printer.println("*** GetFeature: lat=$latitude lon=$longitude")

    val request = Point.newBuilder()
      .setLatitude(latitude)
      .setLongitude(longitude)
      .build()
    val feature = stub.getFeature(request)

    if (feature.exists()) {
      printer.println("Found feature called \"${feature.name}\" at ${feature.location.toStr()}")
    } else {
      printer.println("Found no feature at ${request.toStr()}")
    }
  }

  fun listFeatures(lowLat: Int, lowLon: Int, hiLat: Int, hiLon: Int) = runBlocking {
    printer.println("*** ListFeatures: lowLat=$lowLat lowLon=$lowLon hiLat=$hiLat liLon=$hiLon")

    val request = Rectangle.newBuilder()
      .setLo(point(lowLat, lowLon))
      .setHi(point(hiLat, hiLon))
      .build()
    var i = 1
    stub.listFeatures(request).collect { feature ->
      printer.println("Result #${i++}: $feature")
    }
  }

  fun recordRoute(features: List<Feature>, numPoints: Int) = runBlocking {
    printer.println("*** RecordRoute")
    val requests = flow {
      for (i in 1..numPoints) {
        val feature = features.random(random)
        println("Visiting point ${feature.location.toStr()}")
        emit(feature.location)
        delay(timeMillis = random.nextLong(500L..1500L))
      }
    }
    val finish = launch {
      val summary = stub.recordRoute(requests)
      printer.println("Finished trip with ${summary.pointCount} points.")
      printer.println("Passed ${summary.featureCount} features.")
      printer.println("Travelled ${summary.distance} meters.")
      val duration = summary.elapsedTime.seconds
      printer.println("It took $duration seconds.")
    }
    finish.join()
  }

  fun routeChat() = runBlocking {
    printer.println("*** RouteChat")
    val requestList = listOf(
      RouteNote.newBuilder().apply {
        message = "First message"
        location = point(0, 0)
      }.build(),
      RouteNote.newBuilder().apply {
        message = "Second message"
        location = point(0, 0)
      }.build(),
      RouteNote.newBuilder().apply {
        message = "Third message"
        location = point(1, 0)
      }.build(),
      RouteNote.newBuilder().apply {
        message = "Fourth message"
        location = point(1, 1)
      }.build()
    )
    val requests = requestList.asFlow().onEach { request ->
      printer.println("Sending message \"${request.message}\" at ${request.location.toStr()}")
    }
    val rpc = launch {
      stub.routeChat(requests).collect { note ->
        printer.println("Got message \"${note.message}\" at ${note.location.toStr()}")
      }
      println("Finished RouteChat")
    }

    rpc.join()
  }
}
