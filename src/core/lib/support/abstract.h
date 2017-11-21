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

#ifndef GRPC_CORE_LIB_SUPPORT_ABSTRACT_H
#define GRPC_CORE_LIB_SUPPORT_ABSTRACT_H

// This is needed to support abstract base classes in the c core. Since gRPC
// doesn't have a c++ runtime, it will hit a linker error on delete unless
// we define a virtual operator delete. See this blog for more info:
// https://eli.thegreenplace.net/2015/c-deleting-destructors-and-virtual-operator-delete/
#define GRPC_ABSTRACT_BASE_CLASS \
  static void operator delete(void* p) { abort(); }

#endif /* GRPC_CORE_LIB_SUPPORT_ABSTRACT_H */
