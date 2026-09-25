/*
 *
 * Copyright 2026 gRPC authors.
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

#include <dlfcn.h>

static int grpc_php_nodelete_anchor = 0;

void grpc_php_pin_self_with_nodelete(void) {
  Dl_info info;
  if (dladdr(&grpc_php_nodelete_anchor, &info) && info.dli_fname) {
    void *h = dlopen(info.dli_fname,
                     RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
    (void)h; /* discard; NODELETE flag is now set on the loaded lib */
  }
}
