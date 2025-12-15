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

cdef void sign_trampoline(string_view data_to_sign, SignatureAlgorithm signature_algorithm, OnSignCompletePyWrapper on_sign_complete_py_wrapper, void* completion_data, void* user_data) noexcept:
    cdef PyGILState_STATE gstate
    cdef PyCustomPrivateKeySigner py_signer
    cdef string cpp_signature
    cdef StatusOr[string]* result
    cdef string cpp_error_message
    cdef Status* status
    gstate = PyGILState_Ensure()
    try:
        py_signer = <PyCustomPrivateKeySigner>user_data
        try:
            py_data_to_sign = data_to_sign.data()[:data_to_sign.length()]
            
            # py_sig_alg = None
            # if signature_algorithm == <SignatureAlgorithm>kRsaPss:
            #     py_sig_alg = 'rsa-pss'
            # elif signature_algorithm == <SignatureAlgorithm>kEcdsa:
            #     py_sig_alg = 'ecdsa'
            # else:
            #     raise ValueError("Unknown signature algorithm")
            
            signature = py_signer._py_callable(py_data_to_sign, signature_algorithm)
            if not isinstance(signature, bytes):
                raise TypeError("Signature must be bytes")

            cpp_signature = signature
            result = new StatusOr[string](cpp_signature)
            on_sign_complete_py_wrapper(dereference(result), completion_data)
            del result

        except Exception as e:
            error_message = f"Error in private key offload sign callable: {e}"
            cpp_error_message = <string>error_message.encode('utf-8')
            status = new Status(kUnknown, string_view(cpp_error_message.c_str(), cpp_error_message.length()))
            result = new StatusOr[string](dereference(status))
            on_sign_complete_py_wrapper(dereference(result), completion_data)
            del status
            del result
    finally:
        PyGILState_Release(gstate)


cdef class PyCustomPrivateKeySigner:
    def __cinit__(self, py_callable):
        if not callable(py_callable):
            raise TypeError("py_callable must be callable")
        self._py_callable = py_callable
        
        cpython.Py_INCREF(self)
        self.c_signer = BuildCustomPrivateKeySigner(sign_trampoline, <void*>self)
        if self.c_signer == NULL:
            cpython.Py_DECREF(self)
            raise MemoryError("Failed to create CustomPrivateKeySigner")

    def __dealloc__(self):
        if self.c_signer != NULL:
            del self.c_signer
            self.c_signer = NULL
            cpython.Py_DECREF(self)

    cdef CustomPrivateKeySigner* c_ptr(self):
        return self.c_signer
