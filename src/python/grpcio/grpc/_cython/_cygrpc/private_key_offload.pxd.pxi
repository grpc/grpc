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
# distutils: language=c++

from libcpp.string cimport string
from libcpp.memory cimport shared_ptr, make_shared

cdef extern from "absl/status/status.h" namespace "absl":
    cdef enum AbslStatusCode "absl::StatusCode":
        kOk "absl::StatusCode::kOk"
        kUnknown "absl::StatusCode::kUnknown"
        kInvalidArgument "absl::StatusCode::kInvalidArgument"

    cdef cppclass Status:
        Status(AbslStatusCode, string)
        Status()

    # Status InternalError(const string& message)

cdef extern from "absl/status/statusor.h" namespace "absl":
    cdef cppclass StatusOr[T]:
        StatusOr()
        StatusOr(const T&)
        StatusOr(const Status&)
        bint ok()
        const T& operator*() const
        Status status()

cdef extern from "absl/strings/string_view.h" namespace "absl":
    cdef cppclass string_view:
        string_view(const char* s)
        string_view(const char* s, size_t len)
        const char* data()
        size_t length()

cdef extern from "grpc/private_key_signer.h" namespace "grpc_core":
    cdef cppclass PrivateKeySigner:
        pass
    cdef cppclass AsyncSigningHandle:
        AsyncSigningHandle()
    cdef enum class CSignatureAlgorithm "grpc_core::PrivateKeySigner::SignatureAlgorithm":
        kRsaPkcs1Sha256,
        kRsaPkcs1Sha384,
        kRsaPkcs1Sha512,
        kEcdsaSecp256r1Sha256,
        kEcdsaSecp384r1Sha384,
        kEcdsaSecp521r1Sha512,
        kRsaPssRsaeSha256,
        kRsaPssRsaeSha384,
        kRsaPssRsaeSha512

cdef extern from "grpc/private_key_signer.h":
    cdef void grpc_tls_identity_pairs_add_pair_with_signer(
        grpc_tls_identity_pairs* pairs,
        shared_ptr[PrivateKeySigner] private_key_signer,
        const char* cert_chain)

cdef class PyAsyncSigningHandle:
    cdef shared_ptr[AsyncSigningHandle] c_handle # Pointer to the wrapped C instance

    # def __cinit__(self):
    #     self.c_instance = AsyncSigningHandle()
    #     if self.c_instance == NULL:
    #         raise MemoryError("Failed to create MyCClass instance")

    # def __dealloc__(self):
    #     if self.c_instance != NULL:
    #         # destroy_my_c_class(self.c_instance)
    #         self.c_instance = NULL


cpdef enum SignatureAlgorithm:
    RSA_PKCS1_SHA256 = <int>CSignatureAlgorithm.kRsaPkcs1Sha256
    RSA_PKCS1_SHA384 = <int>CSignatureAlgorithm.kRsaPkcs1Sha384
    RSA_PKCS1_SHA512 = <int>CSignatureAlgorithm.kRsaPkcs1Sha512
    ECDSA_SECP256R1_SHA256 = <int>CSignatureAlgorithm.kEcdsaSecp256r1Sha256
    ECDSA_SECP384R1_SHA384 = <int>CSignatureAlgorithm.kEcdsaSecp384r1Sha384
    ECDSA_SECP521R1_SHA512 = <int>CSignatureAlgorithm.kEcdsaSecp521r1Sha512
    RSA_PSS_RSAE_SHA256 = <int>CSignatureAlgorithm.kRsaPssRsaeSha256
    RSA_PSS_RSAE_SHA384 = <int>CSignatureAlgorithm.kRsaPssRsaeSha384
    RSA_PSS_RSAE_SHA512 = <int>CSignatureAlgorithm.kRsaPssRsaeSha512
    


cdef extern from "src/core/tsi/private_key_signer_py_wrapper.h" namespace "grpc_core":
    cdef cppclass PrivateKeySignerPyWrapperResult:
        StatusOr[string] sync_result
        shared_ptr[AsyncSigningHandle] async_handle
    ctypedef PrivateKeySignerPyWrapperResult(*SignWrapperForPy)(string_view, CSignatureAlgorithm, void*) noexcept
    shared_ptr[PrivateKeySigner] BuildPrivateKeySigner(SignWrapperForPy, void*)
