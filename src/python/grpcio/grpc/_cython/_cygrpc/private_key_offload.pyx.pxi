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


cdef Status MakeInternalError(string message):
    return Status(AbslStatusCode.kUnknown, message)

cdef StatusOr[string] async_modify_wrapper(string_view inp, CSignatureAlgorithm algorithm, void* user_data) noexcept:
  print(f"pyx async_modify_wrapper() starting")
  # Get the original python function the user passes
  py_user_func = <object>user_data

  # Call the user's Python function
  py_result = None
  cdef PyGILState_STATE gstate
  gstate = PyGILState_Ensure()
  try:
    # TODO: what's going on with completion_data
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
  finally:
    PyGILState_Release(gstate)

cdef shared_ptr[PrivateKeySigner] build_private_key_signer(py_user_func):
  # Build the PyPrivateKeySigner WRapper
  py_private_key_signer = BuildPrivateKeySigner(async_modify_wrapper, <void*>py_user_func)
  return py_private_key_signer

# TODO this will actually be some kind of ssl_option setter
# def TODO_API(inp, py_user_func):


