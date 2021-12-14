// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

bool BuiltUnderValgrind() {
#ifdef RUNNING_ON_VALGRIND
  return true;
#else
  return false;
#endif
}

bool BuiltUnderTsan() {
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
  return true;
#else
  return false;
#endif
#else
#ifdef THREAD_SANITIZER
  return true;
#else
  return false;
#endif
#endif
}

bool BuiltUnderAsan() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  return true;
#else
  return false;
#endif
#else
#ifdef ADDRESS_SANITIZER
  return true;
#else
  return false;
#endif
#endif
}

bool BuiltUnderMsan() {
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
  return true;
#else
  return false;
#endif
#else
#ifdef MEMORY_SANITIZER
  return true;
#else
  return false;
#endif
#endif
}

bool BuiltUnderUbsan() {
#ifdef GRPC_UBSAN
  return true;
#else
  return false;
#endif
}
