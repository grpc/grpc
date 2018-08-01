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

#ifndef GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_LOAD_SYSTEM_ROOTS_LINUX_H
#define GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_LOAD_SYSTEM_ROOTS_LINUX_H

#include <grpc/support/port_platform.h>

#ifdef GPR_LINUX

namespace grpc_core {

class SystemRootCerts {
 public:
  // Returns a grpc_slice containing OS-specific root certificates.
  // Protected for testing.
  static grpc_slice GetSystemRootCerts();

  // Creates a bundle slice containing the contents of all certificate files in
  // a directory.
  // Returns such slice.
  // Protected for testing.
  static grpc_slice CreateRootCertsBundle(const char* certs_directory);

  // List of possible Linux certificate files and directories.
  static const char* linux_cert_files_[];
  static const char* linux_cert_directories_[];

 protected:
  // Detect the OS platform and set the platform variable to it.
  // Protected for testing.
  static void DetectPlatform();

  // Gets the absolute file path needed to load a certificate file.
  // This function is not thread-safe.
  // Returns such path.
  // Protected for testing.
  static void GetAbsoluteFilePath(const char* path_buffer,
                                  const char* valid_file_dir,
                                  const char* file_entry_name);

  // Computes the total size of a directory given a path to it.
  // Returns such size.
  // Protected for testing.
  static size_t GetDirectoryTotalSize(const char* directory_path);
};

}  // namespace grpc_core

#endif /* GPR_LINUX */
#endif /* GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_LOAD_SYSTEM_ROOTS_LINUX_H \
        */
