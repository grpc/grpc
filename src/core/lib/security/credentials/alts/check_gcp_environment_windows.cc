/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"

#ifdef GPR_WINDOWS

#include <shellapi.h>
#include <tchar.h>
#include <windows.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#define GRPC_ALTS_WINDOWS_CHECK_COMMAND "powershell.exe"
#define GRPC_ALTS_WINDOWS_CHECK_COMMAND_ARGS \
  "(Get-WmiObject -Class Win32_BIOS).Manufacturer"
#define GRPC_ALTS_WINDOWS_CHECK_BIOS_FILE "windows_bios.data"

static int compute_engine_detection_done = 0;
static bool is_on_compute_engine = false;

namespace grpc_core {
namespace internal {

bool check_bios_data(const char* bios_data_file) {
  char* bios_data = read_bios_file(bios_data_file);
  bool result = !strcmp(bios_data, GRPC_ALTS_EXPECT_NAME_GOOGLE);
  remove(GRPC_ALTS_WINDOWS_CHECK_BIOS_FILE);
  gpr_free(bios_data);
  return result;
}

}  // namespace internal
}  // namespace grpc_core

static bool run_powershell() {
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE;
  HANDLE h = CreateFile(_T(GRPC_ALTS_WINDOWS_CHECK_BIOS_FILE), GENERIC_WRITE,
                        FILE_SHARE_WRITE | FILE_SHARE_READ, &sa, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    gpr_log(GPR_ERROR, "CreateFile failed (%d).", GetLastError());
    return false;
  }
  PROCESS_INFORMATION pi;
  STARTUPINFO si;
  DWORD flags = CREATE_NO_WINDOW;
  ZeroMemory(&pi, sizeof(pi));
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags |= STARTF_USESTDHANDLES;
  si.hStdInput = NULL;
  si.hStdError = h;
  si.hStdOutput = h;
  TCHAR cmd[kBiosDataBufferSize];
  _sntprintf(cmd, kBiosDataBufferSize, _T("%s %s"),
             _T(GRPC_ALTS_WINDOWS_CHECK_COMMAND),
             _T(GRPC_ALTS_WINDOWS_CHECK_COMMAND_ARGS));
  if (!CreateProcess(NULL, cmd, NULL, NULL, TRUE, flags, NULL, NULL, &si,
                     &pi)) {
    gpr_log(GPR_ERROR, "CreateProcess failed (%d).\n", GetLastError());
    return false;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(h);
  return true;
}

bool is_running_on_gcp() {
  if (compute_engine_detection_done) {
    return is_on_compute_engine;
  }
  compute_engine_detection_done = 1;
  bool result = run_powershell() && grpc_core::internal::check_bios_data(
                                        GRPC_ALTS_WINDOWS_CHECK_BIOS_FILE);
  is_on_compute_engine = result;
  return result;
}

#endif  // GPR_WINDOWS
