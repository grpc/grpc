/* This is just a compilation test, to see if we have a version of OpenSSL with
   ALPN support installed. */

#include <stdlib.h>
#include <openssl/ssl.h>

int main() {
  SSL_get0_alpn_selected(NULL, NULL, NULL);
  return 0;
}
