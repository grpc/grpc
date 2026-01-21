# Copyright 2025 gRPC authors.
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
from cython.operator cimport dereference
from cpython.pystate cimport PyGILState_STATE, PyGILState_Ensure, PyGILState_Release
import faulthandler

faulthandler.enable()


cdef Status MakeInternalError(string message):
    return Status(AbslStatusCode.kUnknown, message)

cdef StatusOr[string] async_sign_wrapper(string_view inp, CSignatureAlgorithm algorithm, void* user_data) noexcept nogil:
  # Get the original python function the user passes
  with gil:
    print("GREG: async_sign_wrapper() starting", flush=True)
    print("GREG user data is NULL: ", user_data == NULL, flush=True)
    py_user_func = <object>user_data
    print("GREG: casted py_user_func", flush=True)

    # Call the user's Python function
    py_result = None
    try:
      py_result = py_user_func(inp.data().decode('utf-8'), algorithm)
      if isinstance(py_result, str):
        cpp_string = py_result.encode('utf-8')
        return StatusOr[string](<string>cpp_string)
      elif isinstance(py_result, Exception):
        # If python returns an exception, convert to absl::Status
        cpp_string = str(py_result).encode('utf-8')
        return StatusOr[string](MakeInternalError(cpp_string))
      else:
        cpp_string = f"Invalid result type: {type(py_result)}".encode('utf-8')
        return StatusOr[string](MakeInternalError(cpp_string))

    except Exception as e:
      print(f"Exception in user function: {e}")
      return StatusOr[string](MakeInternalError(f"Exception in user function: {e}".encode('utf-8')))
    # finally:
    #   PyGILState_Release(gstate)

cdef shared_ptr[PrivateKeySigner] build_private_key_signer(py_user_func):
  print("GREG: build_private_key_signer", flush=True)
  print("GREG: build_private_key_signer py_user_func is null? ", <void*>py_user_func == NULL, flush=True)

  # Build the PyPrivateKeySigner WRapper
  # Py_INCREF(py_user_func)
  py_private_key_signer = BuildPrivateKeySigner(async_sign_wrapper, <void*>py_user_func)
  return py_private_key_signer

# cdef void grpc_tls_identity_pairs_add_pair_with_signer(
#     grpc_tls_identity_pairs* pairs,
#     shared_ptr[PrivateKeySigner] private_key_signer,
#     const char* cert_chain)


