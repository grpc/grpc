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

#ifndef GRPC_CORE_LIB_SUPPORT_FORK_H
#define GRPC_CORE_LIB_SUPPORT_FORK_H

/*
 * NOTE: FORKING IS NOT GENERALLY SUPPORTED, THIS IS ONLY INTENDED TO WORK
 *       AROUND VERY SPECIFIC USE CASES.
 */

void grpc_fork_support_init(void);

int grpc_fork_support_enabled(void);

// Test only:  Must be called before grpc_init(), and overrides
// environment variables/compile flags
void grpc_enable_fork_support(int enable);

#endif /* GRPC_CORE_LIB_SUPPORT_FORK_H */
