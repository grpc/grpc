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

#ifndef GRPC_IMPL_CODEGEN_FORK_H
#define GRPC_IMPL_CODEGEN_FORK_H

/**
 * gRPC applications should call this before calling fork().  There should be no
 * active gRPC function calls between calling grpc_prefork() and
 * grpc_postfork_parent()/grpc_postfork_child().
 *
 * Returns 1 on success, 0 otherwise.
 *
 * Typical use:
 * assert(grpc_prefork() == 1);
 * int pid = fork();
 * if (pid) {
 *  grpc_postfork_parent();
 *  // Parent process..
 * } else {
 *  grpc_postfork_child();
 *  // Child process...
 * }
 */

int grpc_prefork();

void grpc_postfork_parent();

void grpc_postfork_child();

void grpc_postfork_parent();

#endif /* GRPC_IMPL_CODEGEN_FORK_H */
