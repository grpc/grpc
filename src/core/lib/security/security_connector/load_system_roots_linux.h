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

#ifdef GPR_LINUX

namespace grpc_core {

class SystemRootCerts {
  // Is this needed?
 public:
  // Returns a grpc_slice containing OS-specific root certificates.
  // Protected for testing.
  static grpc_slice GetSystemRootCerts();

  // Creates a bundle slice containing the contents of all certificate files in
  // a directory.
  // Returns such slice.
  // Protected for testing.
  static grpc_slice CreateRootCertsBundle();

 protected:
  // Detect the OS platform and set the platform variable to it.
  // Protected for testing.
  static void DetectPlatform();

  // Looks for a valid directory to load multiple certificates from.
  // Returns such path or nullptr otherwise.
  // Protected for testing.
  static const char* GetValidCertsDirectory();

  // Gets the absolute file path needed to load a certificate file.
  // This function is not thread-safe.
  // Returns such path.
  // Protected for testing.
  static const char* GetAbsoluteFilePath(const char* valid_file_dir,
                                         const char* file_entry_name);

  // Computes the total size of a directory given a path to it.
  // Returns such size.
  // Protected for testing.
  static size_t GetDirectoryTotalSize(const char* directory_path);

 private:
  // List of possible Linux certificate files and directories.
  static const char* linux_cert_files_[];
  static const char* linux_cert_directories_[];
};

}  // namespace grpc_core

#endif /* GPR_LINUX */