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

package io.grpc.examples.routeguide;

import com.google.common.io.ByteSource
import com.google.common.io.Resources
import com.google.protobuf.util.JsonFormat

internal fun defaultFeatureSource(): ByteSource =
  Resources.asByteSource(Resources.getResource("example/resources/io/grpc/examples/routeguide/route_guide_db.json"))

internal fun ByteSource.parseJsonFeatures(): List<Feature> =
  asCharSource(Charsets.UTF_8)
    .openBufferedStream()
    .use { reader ->
      FeatureDatabase.newBuilder().apply {
        JsonFormat.parser().merge(reader, this)
      }.build().featureList
    }
