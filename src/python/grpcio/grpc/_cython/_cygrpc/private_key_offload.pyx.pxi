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
from typing import Callable, Optional, Union

PrivateKeySignCancel = Callable[[], None]

cdef StatusOr[string] MakeInternalError(string message):
    return StatusOr[string](Status(AbslStatusCode.kUnknown, message))

cdef StatusOr[string] MakeStringResult(string result):
  return StatusOr[string](result)

# cdef class PyAsyncSigningHandleImpl(PyAsyncSigningHandle):
#     cdef shared_ptr[AsyncSigningHandle] c_handle # Pointer to the wrapped C instance
#     cdef dict __dict__

#     def __cinit__(self):
#         cdef shared_ptr[AsyncSigningHandlePyWrapper] py_wrapper_handle = make_shared[AsyncSigningHandlePyWrapper]()
#         py_wrapper_handle.get().python_handle = <void*> self
#         # Make sure `self` lives long enough for being called through C-Core
#         Py_INCREF(self)
#         self.c_handle = static_pointer_cast[AsyncSigningHandle, AsyncSigningHandlePyWrapper](py_wrapper_handle)

#     def __dealloc__(self):
#       Py_DECREF(self)

cdef void python_object_decref_callback(void* ptr) noexcept nogil:
    # We are being called from C++, potentially without the GIL.
  with gil:
    try:
      if ptr != NULL:
        obj = <object>ptr
        Py_DECREF(obj)
    except:
      pass

cdef class OnCompleteWrapper:
  cdef CompletionFunctionPyWrapper c_on_complete
  cdef void* c_completion_data

  # Makes this class callable
  def __call__(self, result):
    cdef StatusOr[string] cpp_result
    cdef string cpp_string
    if self.c_on_complete != NULL:
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
      self.c_on_complete(cpp_result, <void*> self.c_completion_data)
      # # Don't call multiple types
      # self.c_on_complete = NULL

cdef PrivateKeySignerPyWrapperResult async_sign_wrapper(string_view inp, CSignatureAlgorithm algorithm, void* user_data, CompletionFunctionPyWrapper on_complete, void* completion_data) noexcept nogil:
  cdef string cpp_string
  cdef const char* data
  cdef size_t size
  cdef PrivateKeySignerPyWrapperResult cpp_result
  with gil:
    # Cast the void* pointer holding the user's python sign impl
    py_user_func = <object>user_data

    on_complete_wrapper = OnCompleteWrapper()
    on_complete_wrapper.c_on_complete = on_complete
    on_complete_wrapper.c_completion_data = completion_data

    # Call the user's Python function and handle results
    py_result = None
    try:
      data = inp.data()
      size = inp.length()
      py_bytes = PyBytes_FromStringAndSize(data, size)
      py_result = py_user_func(py_bytes, algorithm, on_complete_wrapper)
      cpp_result.is_sync = True
      if isinstance(py_result, bytes):
        print("GREG: result is bytes", flush=True)
        # We got a signature
        cpp_string = py_result
        cpp_result.sync_result = MakeStringResult(cpp_string)
      elif isinstance(py_result, Exception):
        # If python returns an exception, convert to absl::Status
        cpp_string = str(py_result).encode('utf-8')
        cpp_result.sync_result = MakeInternalError(cpp_string)
      elif callable(py_result):
        print("GREG: result is callable", flush=True)
        # Cancellation func
        # Async handle return
        cpp_result.is_sync = False
        cpp_result.async_result.cancel_wrapper = cancel_wrapper
        Py_INCREF(py_result)
        cpp_result.async_result.python_callable = <void*> py_result
        cpp_result.async_result.python_callable_decref = python_object_decref_callback
        # cpp_result.async_handle = async_handle.c_handle
      else:
        print("GREG: result is invalid type", flush=True)
        # Any other return type is not valid
        cpp_string = f"Invalid result type: {type(py_result)}".encode('utf-8')
        cpp_result.sync_result = MakeInternalError(cpp_string)
      return cpp_result
    except Exception as e:
      # If Python raises an exception, make it an error status
      print("GREG: result raised exception", e, flush=True)
      cpp_result.sync_result = MakeInternalError(f"Exception in user function: {e}".encode('utf-8'))
      return cpp_result

cdef void cancel_wrapper(void* cancel_data) noexcept nogil:
  with gil:
    print("GREG: in cancel_wrapper", flush=True)
    try:
      py_cancel_func = <object>cancel_data
      py_cancel_func()
    except Exception as e:
      # Exceptions in cancellation
      pass

    
# cdef void cancel_wrapper(shared_ptr[AsyncSigningHandle] handle, void* cancel_data) noexcept nogil:
#   with gil:
#     try:
#       # Get the Python handle from the C handle
#       impl = <shared_ptr[AsyncSigningHandlePyWrapper]>static_pointer_cast[AsyncSigningHandlePyWrapper, AsyncSigningHandle](handle)
#       py_handle_ptr = impl.get().python_handle
#       py_handle = <PyAsyncSigningHandleImpl>py_handle_ptr
#       # Get the python callable
#       py_cancel_func = <object>cancel_data
#       py_cancel_func(py_handle)
#     except Exception as e:
#       # Exceptions in cancellation
#       pass
    

# To be called from the python layer when the user provides a signer function.
cdef shared_ptr[PrivateKeySigner] build_private_key_signer(py_user_func):
  py_private_key_signer = BuildPrivateKeySigner(async_sign_wrapper, <void*>py_user_func)
  return py_private_key_signer

cdef shared_ptr[PrivateKeySigner] build_private_key_signer_with_cancellation(py_user_func, py_cancellation_func):
  py_private_key_signer = BuildPrivateKeySignerWithCancellation(async_sign_wrapper, <void*>py_user_func, cancel_wrapper, <void*>py_cancellation_func)
  return py_private_key_signer

# def create_async_signing_handle():
#   return PyAsyncSigningHandleImpl()




