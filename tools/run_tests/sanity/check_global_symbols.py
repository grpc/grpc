#!/usr/bin/env python3

# Copyright 2026 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import subprocess
import sys

def check_symbol(mangled_name, demangled_name):
    allowed_c_prefixes = ["grpc_", "tsi_"]
    allowed_cpp_namespaces = ["grpc_core::", "tsi::"]

    if mangled_name.startswith("_GLOBAL__sub_I_"):
        return True
    if mangled_name.startswith("__cxx_global_var_init"):
        return True
    
    if any(mangled_name.startswith(p) for p in allowed_c_prefixes):
        return True

    if any(demangled_name.startswith(ns) for ns in allowed_cpp_namespaces):
        return True

    # Allow compiler generated symbols for allowed namespaces
    allowed_cpp_prefixes = [
        "vtable for ",
        "typeinfo for ",
        "typeinfo name for ",
        "non-virtual thunk to ",
        "virtual thunk to ",
        "guard variable for ",
    ]
    
    for prefix in allowed_cpp_prefixes:
        if demangled_name.startswith(prefix):
            remainder = demangled_name[len(prefix):]
            if any(remainder.startswith(ns) for ns in allowed_cpp_namespaces):
                return True

    return False

def main():
    os.chdir(os.path.join(os.path.dirname(sys.argv[0]), "../../.."))

    # Only run on Linux to avoid issues with missing tools on Windows/Mac CI
    if sys.platform != "linux" and sys.platform != "linux2":
        print("Skipping global symbols check on non-Linux platform.")
        sys.exit(0)

    print("Building //:grpc...")
    try:
        subprocess.check_call(["tools/bazel", "build", "//:grpc"])
    except subprocess.CalledProcessError as e:
        print(f"Error building grpc: {e}")
        sys.exit(1)

    lib_path = "bazel-bin/libgrpc.a"
    if not os.path.exists(lib_path):
        print(f"Error: {lib_path} not found.")
        sys.exit(1)

    print(f"Extracting symbols from {lib_path}...")
    try:
        nm_output = subprocess.check_output(["nm", "-g", "--defined-only", lib_path], text=True)
    except subprocess.CalledProcessError as e:
        print(f"Error running nm: {e}")
        sys.exit(1)
        
    symbols = []
    for line in nm_output.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols.append(parts[-1])
        elif len(parts) == 2:
            symbols.append(parts[-1])

    if not symbols:
        print("No symbols found.")
        sys.exit(1)

    try:
        cxxfilt_proc = subprocess.Popen(["c++filt"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        demangled_output, _ = cxxfilt_proc.communicate("\n".join(symbols))
        demangled_symbols = demangled_output.splitlines()
    except OSError as e:
        print(f"Error running c++filt: {e}")
        sys.exit(1)

    errors = []
    
    # ALLOWLIST for currently polluting symbols
    # We will populate this from the CI output, but keep it empty for now
    # to catch any existing violations.
    allowlist = [
    ]

    for mangled, demangled in zip(symbols, demangled_symbols):
        if not check_symbol(mangled, demangled):
            if mangled not in allowlist and demangled not in allowlist:
                errors.append((mangled, demangled))

    if errors:
        print("The following symbols pollute the global namespace:")
        for mangled, demangled in errors:
            if mangled == demangled:
                print(f"  {mangled}")
            else:
                print(f"  {mangled} ({demangled})")
        print("\nPlease ensure all symbols are within 'grpc_core' or 'tsi' namespaces, or start with 'grpc_' or 'tsi_'.")
        sys.exit(1)

    print("All global symbols are compliant.")

if __name__ == "__main__":
    main()
