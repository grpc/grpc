/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "rb_grpc_imports.generated.h"

#if GPR_WINDOWS
#include <tchar.h>

int grpc_rb_load_core() {
#if GPR_ARCH_64
  TCHAR fname[] = _T("grpc_c.64.ruby");
#else
  TCHAR fname[] = _T("grpc_c.32.ruby");
#endif
  HMODULE module = GetModuleHandle(_T("grpc_c.so"));
  TCHAR path[2048 + 32] = _T("");
  LPTSTR seek_back = NULL;
  GetModuleFileName(module, path, 2048);

  seek_back = _tcsrchr(path, _T('\\'));

  while (seek_back) {
    HMODULE grpc_c;
    _tcscpy(seek_back + 1, fname);
    grpc_c = LoadLibrary(path);
    if (grpc_c) {
      grpc_rb_load_imports(grpc_c);
      return 1;
    } else {
      *seek_back = _T('\0');
      seek_back = _tcsrchr(path, _T('\\'));
    }
  }

  return 0;
}

#else

int grpc_rb_load_core() { return 1; }

#endif
