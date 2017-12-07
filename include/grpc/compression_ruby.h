/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_COMPRESSION_RUBY_H
#define GRPC_COMPRESSION_RUBY_H

#include <grpc/impl/codegen/port_platform.h>

#include <grpc/impl/codegen/compression_types.h>
#include <grpc/slice.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Parses the \a slice as a grpc_compression_algorithm instance and updating \a
 * algorithm following algorithm names compatible with Ruby. Returns 1 upon
 * success, 0 otherwise. */
GRPCAPI int grpc_compression_algorithm_parse_ruby(
    grpc_slice value, grpc_compression_algorithm* algorithm);

/** Updates \a name with the encoding name corresponding to a valid \a
 * algorithm. The \a name follows names compatible with Ruby. Note that \a name
 * is statically allocated and must *not* be freed. Returns 1 upon success, 0
 * otherwise. */
GRPCAPI int grpc_compression_algorithm_name_ruby(
    grpc_compression_algorithm algorithm, const char** name);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_COMPRESSION_RUBY_H */
