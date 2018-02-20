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

#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"

#include <ctype.h>
#include <grpc/grpc.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN64) || defined(WIN64) || defined(_WIN32) || defined(WIN32)
#include <shellapi.h>
#include <tchar.h>
#include <windows.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#define GRPC_ALTS_EXPECT_NAME_GOOGLE "Google"
#define GRPC_ALTS_EXPECT_NAME_GCE "Google Compute Engine"
#define GRPC_ALTS_PRODUCT_NAME_FILE "/sys/class/dmi/id/product_name"
#define GRPC_ALTS_WINDOWS_CHECK_COMMAND "powershell.exe"
#define GRPC_ALTS_WINDOWS_CHECK_COMMAND_ARGS \
  "(Get-WmiObject -Class Win32_BIOS).Manufacturer"
#define GRPC_ALTS_WINDOWS_CHECK_BIOS_FILE "windows_bios.data"

const size_t kBiosDataBufferSize = 256;

static int compute_engine_detection_done = 0;
static bool is_on_compute_engine = false;

static char* trim(char* src) {
  if (src == nullptr) {
    return nullptr;
  }
  char* des = nullptr;
  int start = 0, end = static_cast<int>(strlen(src)) - 1;
  /* find the last character that is not a whitespace. */
  while (end >= 0 && isspace(src[end])) {
    end--;
  }
  /* find the first character that is not a whitespace. */
  while ((size_t)start < strlen(src) && isspace(src[start])) {
    start++;
  }
  if (start <= end) {
    des = static_cast<char*>(
        gpr_zalloc(sizeof(char) * (end - start + 2 /* '\0' */)));
    memcpy(des, src + start, end - start + 1);
  }
  return des;
}

bool check_bios_data(const char* bios_data_file, bool is_linux) {
  FILE* fp = fopen(bios_data_file, "r");
  if (!fp) {
    gpr_log(GPR_ERROR, "BIOS data file cannot be opened.");
    return false;
  }
  char buf[kBiosDataBufferSize + 1];
  bool result = false;
  size_t ret = fread(buf, sizeof(char), kBiosDataBufferSize, fp);
  buf[ret] = '\0';
  char* trimmed_buf = trim(buf);
  fclose(fp);
  if (is_linux) {
    result = (!strcmp(trimmed_buf, GRPC_ALTS_EXPECT_NAME_GOOGLE)) ||
             (!strcmp(trimmed_buf, GRPC_ALTS_EXPECT_NAME_GCE));
  } else {
    result = !strcmp(trimmed_buf, GRPC_ALTS_EXPECT_NAME_GOOGLE);
    remove(GRPC_ALTS_WINDOWS_CHECK_BIOS_FILE);
  }
  gpr_free(trimmed_buf);
  return result;
}

#if defined(_WIN64) || defined(WIN64) || defined(_WIN32) || defined(WIN32)
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
#endif

bool is_running_on_gcp() {
  if (compute_engine_detection_done) {
    return is_on_compute_engine;
  }
  compute_engine_detection_done = 1;
  bool result = false;
#if defined(__linux__)
  result = check_bios_data(GRPC_ALTS_PRODUCT_NAME_FILE, true /* is_linux */);
#elif defined(_WIN64) || defined(WIN64) || defined(_WIN32) || defined(WIN32)
  result =
      run_powershell() &&
      check_bios_data(GRPC_ALTS_WINDOWS_CHECK_BIOS_FILE, false /* is_linux */);
#else
  gpr_log(GPR_ERROR,
          "Platforms other than Linux and Windows are not supported");
#endif
  is_on_compute_engine = result;
  return result;
}
