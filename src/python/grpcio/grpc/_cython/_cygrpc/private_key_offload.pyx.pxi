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
from cpython.bytes cimport PyBytes_FromStringAndSize

cdef StatusOr[string] MakeInternalError(string message):
    return StatusOr[string](Status(AbslStatusCode.kUnknown, message))

cdef StatusOr[string] MakeStringResult(string result):
  return StatusOr[string](result)

cdef PrivateKeySignerPyWrapperResult async_sign_wrapper(string_view inp, CSignatureAlgorithm algorithm, void* user_data) noexcept nogil:
  # Get the original python function the user passes
  cdef string cpp_string
  cdef const char* data
  cdef size_t size
  cdef PrivateKeySignerPyWrapperResult cpp_result
  # We need to hold the GIL to call the python function and interact with python values
  with gil:
    # Cast the void* pointer holding the user's python sign impl
    py_user_func = <object>user_data

    # Call the user's Python function
    py_result = None
    try:
      data = inp.data()
      size = inp.length()
      py_bytes = PyBytes_FromStringAndSize(data, size)
      py_result = py_user_func(py_bytes, algorithm)
      if isinstance(py_result, bytes):
        # We got a signature
        cpp_string = py_result
        cpp_result.sync_result = MakeStringResult(cpp_string)
        # return StatusOr[string](cpp_string)
      elif isinstance(py_result, Exception):
        # If python returns an exception, convert to absl::Status
        cpp_string = str(py_result).encode('utf-8')
        cpp_result.sync_result = MakeInternalError(cpp_string)
        # return StatusOr[string](MakeInternalError(cpp_string))
      else:
        # Any other return type is not valid
        cpp_string = f"Invalid result type: {type(py_result)}".encode('utf-8')
        cpp_result.sync_result = MakeInternalError(cpp_string)
        # return StatusOr[string](MakeInternalError(cpp_string))
      return cpp_result

    except Exception as e:
      # If Python raises an exception, make it an error status
      cpp_result.sync_result = MakeInternalError(f"Exception in user function: {e}".encode('utf-8'))
      return cpp_result
      # return StatusOr[string](MakeInternalError(f"Exception in user function: {e}".encode('utf-8')))
    

# To be called from the python layer when the user provides a signer function.
cdef shared_ptr[PrivateKeySigner] build_private_key_signer(py_user_func):
  py_private_key_signer = BuildPrivateKeySigner(async_sign_wrapper, <void*>py_user_func)
  return py_private_key_signer


