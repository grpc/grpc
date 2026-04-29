/*
 *
 * Copyright 2025 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include "upb/mini_table/extension.h"
#include "upb/mini_table/internal/generated_registry.h"

// We need to include upb/port/def.inc to get UPB_PRIVATE and other macros.
#include "upb/port/def.inc"

/* Define the linker array boundaries with initializers to ensure correct
 * section placement on MSVC. Without initializers, selectany symbols may not
 * be correctly placed in the requested sections ($a and $z), leading to
 * incorrect array boundaries and heap corruption during extension
 * registration.
 *
 * These section names must match UPB_LINKARR_NAME(upb_AllExts, ...)
 */
#pragma section("la_upb_AllExts$a", read)
#pragma section("la_upb_AllExts$z", read)

__declspec(allocate("la_upb_AllExts$a"), selectany)
const struct upb_MiniTableExtension __start_linkarr_upb_AllExts = {{0}};

__declspec(allocate("la_upb_AllExts$z"), selectany)
const struct upb_MiniTableExtension __stop_linkarr_upb_AllExts = {{0}};

/* Provide a "master" constructor that runs and registers the extensions.
 * Since this constructor is initialized and selectany, it will be preferred
 * by the linker over the uninitialized ones in the upb headers, and it
 * correctly uses the boundaries defined above.
 */
extern const UPB_PRIVATE(upb_GeneratedExtensionListEntry)*
    UPB_PRIVATE(upb_generated_extension_list);

static void __cdecl upb_GeneratedRegistry_Constructor(void) {
  static bool finished = false;
  if (finished) return;
  finished = true;
  static UPB_PRIVATE(upb_GeneratedExtensionListEntry) entry;
  entry.start = &__start_linkarr_upb_AllExts;
  entry.stop = &__stop_linkarr_upb_AllExts;
  entry.next = UPB_PRIVATE(upb_generated_extension_list);
  UPB_PRIVATE(upb_generated_extension_list) = &entry;
}

#pragma section(".CRT$XCU", long, read)
__declspec(allocate(".CRT$XCU"), selectany) void(__cdecl *
                                                 upb_GeneratedRegistry_Constructor_ptr)(
    void) = upb_GeneratedRegistry_Constructor;

#include "upb/port/undef.inc"

#endif
