<?hh

<<__NativeData("ServerCredentials")>>
class ServerCredentials {

  /**
   * Create SSL credentials.
   * @param string pem_root_certs PEM encoding of the server root certificates
   * @param string pem_private_key PEM encoding of the client's private key
   * @param string pem_cert_chain PEM encoding of the client's certificate chain
   * @return Credentials The new SSL credentials object
   */
  <<__Native>>
  public function createSsl(string $pem_root_certs, string $pem_private_key, string $pem_cert_chain): object;

}
