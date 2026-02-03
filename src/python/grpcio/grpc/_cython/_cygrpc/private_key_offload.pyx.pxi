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
from cpython.bytes cimport PyBytes_FromStringAndSize
from libcpp.memory cimport make_shared, static_pointer_cast

cdef StatusOr[string] MakeInternalError(string message):
    return StatusOr[string](Status(AbslStatusCode.kUnknown, message))

cdef StatusOr[string] MakeStringResult(string result):
  return StatusOr[string](result)

cdef class OnCompleteWrapper:
  cdef CompletionFunctionPyWrapper on_complete_wrapper
  cdef void* c_on_complete_fn

  # Makes this class callable
  def __call__(self, result):
    cdef StatusOr[string] cpp_result
    cdef string cpp_string
    if self.on_complete_wrapper != NULL:
      if isinstance(result, bytes):
        # We got a signature
        cpp_string = result
        cpp_result = MakeStringResult(cpp_string)
      elif isinstance(result, Exception):
        # If python returns an exception, convert to absl::Status
        cpp_string = str(result).encode('utf-8')
        cpp_result = MakeInternalError(cpp_string)
      else:
        # Any other return type is not valid
        cpp_string = f"Invalid result type: {type(result)}".encode('utf-8')
        cpp_result = MakeInternalError(cpp_string)
      self.on_complete_wrapper(cpp_result, <void*> self.c_on_complete_fn)
      # # Don't call multiple types
      # self.c_on_complete = NULL

cdef PrivateKeySignerPyWrapperResult async_sign_wrapper(string_view inp, CSignatureAlgorithm algorithm, void* py_user_sign_fn, CompletionFunctionPyWrapper on_complete_wrapper, void* c_on_complete_fn) noexcept nogil:
  cdef string cpp_string
  cdef const char* data
  cdef size_t size
  cdef PrivateKeySignerPyWrapperResult cpp_result
  with gil:
    # Cast the void* pointer holding the user's python sign impl
    py_user_func = <object>py_user_sign_fn

    py_on_complete_wrapper = OnCompleteWrapper()
    py_on_complete_wrapper.on_complete_wrapper = on_complete_wrapper
    py_on_complete_wrapper.c_on_complete_fn = c_on_complete_fn 

    # Call the user's Python function and handle results
    py_result = None
    try:
      data = inp.data()
      size = inp.length()
      py_bytes = PyBytes_FromStringAndSize(data, size)
      py_result = py_user_func(py_bytes, algorithm, py_on_complete_wrapper)
      cpp_result.is_sync = True
      if isinstance(py_result, bytes):
        # We got a signature
        cpp_string = py_result
        cpp_result.sync_result = MakeStringResult(cpp_string)
      elif isinstance(py_result, Exception):
        # If python returns an exception, convert to absl::Status
        cpp_string = str(py_result).encode('utf-8')
        cpp_result.sync_result = MakeInternalError(cpp_string)
      elif callable(py_result):
        # Cancellation func
        cpp_result.is_sync = False
        Py_INCREF(py_result)
        cpp_result.async_result.py_user_cancel_fn = <void*> py_result
        cpp_result.async_result.cancel_wrapper = cancel_wrapper
      else:
        # Any other return type is not valid
        cpp_string = f"Invalid result type: {type(py_result)}".encode('utf-8')
        cpp_result.sync_result = MakeInternalError(cpp_string)
      return cpp_result
    except Exception as e:
      # If Python raises an exception, make it an error status
      cpp_result.sync_result = MakeInternalError(f"Exception in user function: {e}".encode('utf-8'))
      return cpp_result

cdef void cancel_wrapper(void* py_cancel_user_fn) noexcept nogil:
  with gil:
    try:
      py_cancel_func = <object>py_cancel_user_fn
      py_cancel_func()
    except Exception as e:
      # Exceptions in cancellation
      pass
    

# To be called from the python layer when the user provides a signer function.
cdef shared_ptr[PrivateKeySigner] build_private_key_signer(py_user_func):
  py_private_key_signer = BuildPrivateKeySigner(async_sign_wrapper, <void*>py_user_func)
  return py_private_key_signer