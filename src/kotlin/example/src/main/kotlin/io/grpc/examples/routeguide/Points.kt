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

import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt


private const val EARTH_RADIUS_IN_M = 6371000

private fun Int.toRadians() = Math.toRadians(toDouble())

internal infix fun Point.distanceTo(other: Point): Int {
  val lat1 = latitude.toRadians()
  val long1 = longitude.toRadians()
  val lat2 = other.latitude.toRadians()
  val long2 = other.latitude.toRadians()

  val dLat = lat2 - lat1
  val dLong = long2 - long1

  val a = sin(dLat / 2).pow(2) + cos(lat1) * cos(lat2) * sin(dLong / 2).pow(2)
  val c = 2 * atan2(sqrt(a), sqrt(1 - a))
  return (EARTH_RADIUS_IN_M * c).roundToInt()
}

internal operator fun Rectangle.contains(p: Point): Boolean {
  val lowLong = minOf(lo.longitude, hi.longitude)
  val hiLong = maxOf(lo.longitude, hi.longitude)
  val lowLat = minOf(lo.latitude, hi.latitude)
  val hiLat = maxOf(lo.latitude, hi.latitude)
  return p.longitude in lowLong..hiLong && p.latitude in lowLat..hiLat
}

private fun Int.normalizeCoordinate(): Double = this / 1.0e7

internal fun Point.toStr(): String {
  val lat = latitude.normalizeCoordinate()
  val long = longitude.normalizeCoordinate()
  return "$lat, $long"
}

internal fun Feature.exists(): Boolean = name.isNotEmpty()
