//
// Created by itsemmanuel on 7/16/21.
// TODO: Replace with appropriate comments.
//

#ifndef SRC_CORE_TSI_OPENSSL_DELETERS_H_
#define SRC_CORE_TSI_OPENSSL_DELETERS_H_

#include "openssl/bio.h"
#include "openssl/evp.h"
#include "openssl/x509.h"

namespace grpc_core {

struct EVP_PKEYDeleter {
  void operator()(EVP_PKEY* pkey) const { EVP_PKEY_free(pkey); }
};
struct BIO_Deleter {
  void operator()(BIO* bio) const { BIO_free(bio); }
};
struct X509_Deleter {
  void operator()(X509* x509) const { X509_free(x509); }
};

}  // namespace grpc_core

#endif  // SRC_CORE_TSI_OPENSSL_DELETERS_H_
