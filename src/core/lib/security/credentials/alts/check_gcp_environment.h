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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_ALTS_CHECK_GCP_ENVIRONMENT_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_ALTS_CHECK_GCP_ENVIRONMENT_H

/**
 * This method checks if system BIOS data contains Google-specific phrases.
 * Exposed for testing only.
 *
 * - bios_data_file: a file containing BIOS data used to determine GCE tenancy
 *   information.
 * - is_linux: a boolean flag indicating if the API is invoked on a Linux
 *   platform or not.
 *
 * It returns true if the BIOS data contains Google-specific phrases, and false
 * otherwise.
 */
bool check_bios_data(const char* bios_data_file, bool is_linux);
/**
 * This method checks if a VM (Windows or Linux) is running within Google
 * compute Engine (GCE) or not. It returns true if the VM is running in GCE and
 * false otherwise.
 */
bool is_running_on_gcp();
#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_ALTS_CHECK_GCP_ENVIRONMENT_H */
