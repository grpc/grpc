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
# from libcpp.memory import static_pointer_cast
from libc.stdio cimport printf

import faulthandler

faulthandler.enable()

cdef StatusOr[string] MakeInternalError(string message):
    return StatusOr[string](Status(AbslStatusCode.kUnknown, message))

cdef StatusOr[string] MakeStringResult(string result):
  return StatusOr[string](result)

cdef class PyAsyncSigningHandleImpl(PyAsyncSigningHandle):
    cdef shared_ptr[AsyncSigningHandle] c_handle # Pointer to the wrapped C instance
    cdef dict __dict__

    def __cinit__(self):
        cdef shared_ptr[AsyncSigningHandlePyWrapper] py_wrapper_handle = make_shared[AsyncSigningHandlePyWrapper]()
        # might need to incref here
        py_wrapper_handle.get().python_handle = <void*> self
        Py_INCREF(self)
        self.c_handle = static_pointer_cast[AsyncSigningHandle, AsyncSigningHandlePyWrapper](py_wrapper_handle)

    def __dealloc__(self):
      print("GREG: in handle __dealloc__", flush=True)
      # Maybe need to handle shared_ptr here
      # Py_DECREF(self)

cdef class OnCompleteWrapper:
  cdef CompletionFunctionPyWrapper c_on_complete
  cdef void* c_completion_data

  # Makes this class callable
  def __call__(self, result):
    print(f"Python OnCompleteWrapper({result}) starting", flush=True)
    cdef StatusOr[string] cpp_result
    cdef string cpp_string
    if self.c_on_complete != NULL:
      if isinstance(result, bytes):
        # We got a signature
        cpp_string = result
        cpp_result = MakeStringResult(cpp_string)
        # return StatusOr[string](cpp_string)
      elif isinstance(result, Exception):
        # If python returns an exception, convert to absl::Status
        cpp_string = str(result).encode('utf-8')
        cpp_result = MakeInternalError(cpp_string)
      else:
        # Any other return type is not valid
        print("GREG: in else of async sign", flush=True)
        cpp_string = f"Invalid result type: {type(result)}".encode('utf-8')
        cpp_result = MakeInternalError(cpp_string)
        # return StatusOr[string](MakeInternalError(cpp_string))
      self.c_on_complete(cpp_result, <void*> self.c_completion_data)
      # Don't call multiple types
      self.c_on_complete = NULL
    print(f"Python OnCompleteWrapper({result}) ending", flush=True)

cdef PrivateKeySignerPyWrapperResult async_sign_wrapper(string_view inp, CSignatureAlgorithm algorithm, void* user_data, CompletionFunctionPyWrapper on_complete, void* completion_data) noexcept nogil:
  # Get the original python function the user passes
  cdef string cpp_string
  cdef const char* data
  cdef size_t size
  cdef PrivateKeySignerPyWrapperResult cpp_result
  # cdef OnCompleteWrapper on_complete_wrapper
  # We need to hold the GIL to call the python function and interact with python values
  with gil:
    # Cast the void* pointer holding the user's python sign impl
    print("GREG: in async_sign_wrapper", flush=True)
    py_user_func = <object>user_data

    on_complete_wrapper = OnCompleteWrapper()
    on_complete_wrapper.c_on_complete = on_complete
    on_complete_wrapper.c_completion_data = completion_data
    ## TODO(gregorycooke): do work here - Completion stuff has been added to the function signature but no impl done

    # Call the user's Python function
    py_result = None
    try:
      data = inp.data()
      size = inp.length()
      py_bytes = PyBytes_FromStringAndSize(data, size)
      py_result = py_user_func(py_bytes, algorithm, on_complete_wrapper, "")
      if isinstance(py_result, PyAsyncSigningHandle):
        print("async return from user func", flush=True)
        async_handle = <PyAsyncSigningHandleImpl>py_result
        cpp_result.async_handle = async_handle.c_handle
      elif isinstance(py_result, bytes):
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
        print("GREG: in else of async sign", flush=True)
        cpp_string = f"Invalid result type: {type(py_result)}".encode('utf-8')
        cpp_result.sync_result = MakeInternalError(cpp_string)
        # return StatusOr[string](MakeInternalError(cpp_string))
      return cpp_result

    except Exception as e:
      print("GREG: in except of async sign: ", e, flush=True)
      # If Python raises an exception, make it an error status
      cpp_result.sync_result = MakeInternalError(f"Exception in user function: {e}".encode('utf-8'))
      return cpp_result
      # return StatusOr[string](MakeInternalError(f"Exception in user function: {e}".encode('utf-8')))
    
cdef void cancel_wrapper(shared_ptr[AsyncSigningHandle] handle, void* cancel_data) noexcept nogil:
  printf("GREG: In cancel_wrapper!!!!\n")
  # cdef shared_ptr[AsyncSigningHandlePyWrapper] impl = static_pointer_cast[AsyncSigningHandlePyWrapper, AsyncSigningHandle](handle)
  # cdef void* py_handle_ptr = impl.get().python_handle
  # cdef shared_ptr[AsyncSigningHandlePyWrapper] impl
  with gil:
    try:
      print("GREG: In cancel_wrapper in gil", flush=True)
      impl = <shared_ptr[AsyncSigningHandlePyWrapper]>static_pointer_cast[AsyncSigningHandlePyWrapper, AsyncSigningHandle](handle)
      py_handle_ptr = impl.get().python_handle
      py_handle = <PyAsyncSigningHandleImpl>py_handle_ptr
      py_cancel_func = <object>cancel_data
      print("Calling py_cancel_func", flush=True)
      py_cancel_func(py_handle)
    except Exception as e:
      print("GREG: exception", e, flush=True)
    

# To be called from the python layer when the user provides a signer function.
cdef shared_ptr[PrivateKeySigner] build_private_key_signer(py_user_func):
  py_private_key_signer = BuildPrivateKeySigner(async_sign_wrapper, <void*>py_user_func)
  return py_private_key_signer

cdef shared_ptr[PrivateKeySigner] build_private_key_signer_with_cancellation(py_user_func, py_cancellation_func):
  printf("GREG: build signer printf\n")
  py_private_key_signer = BuildPrivateKeySignerWithCancellation(async_sign_wrapper, <void*>py_user_func, cancel_wrapper, <void*>py_cancellation_func)
  return py_private_key_signer


# cdef shared_ptr[AsyncSigningHandle] create_async_signing_handle():
#   return make_shared[AsyncSigningHandleImpl]()

def create_async_signing_handle():
  return PyAsyncSigningHandleImpl()




