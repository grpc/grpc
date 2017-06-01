<?hh

<<__NativeData("ChannelCredentials")>>
class ChannelCredentials {

  /**
   * Set default roots pem.
   * @param string $pem_roots PEM encoding of the server root certificates
   * @return void
   */
  <<__Native>>
  public function setDefaultRootsPem(string $pem_roots): void;

  /**
   * Create a default channel credentials object.
   * @return ChannelCredentials The new default channel credentials object
   */
  <<__Native>>
  public function createDefault(): ChannelCredentials;

  /**
   * Create SSL credentials.
   * @param string $pem_root_certs PEM encoding of the server root certificates
   * @param string $pem_key_cert_pair.private_key PEM encoding of the client's
   *                                              private key (optional)
   * @param string $pem_key_cert_pair.cert_chain PEM encoding of the client's
   *                                             certificate chain (optional)
   * @return ChannelCredentials The new SSL credentials object
   */
  <<__Native>>
  public function createSsl(string $pem_root_certs,
    ?string $pem_key_cert_pair__private_key = null,
    ?string $pem_key_cert_pair__cert_chain = null): ChannelCredentials;

  /**
   * Create composite credentials from two existing credentials.
   * @param ChannelCredentials $cred1_obj The first credential
   * @param CallCredentials $cred2_obj The second credential
   * @return ChannelCredentials The new composite credentials object
   */
  <<__Native>>
  public function createComposite(ChannelCredentials $cred1_obj, CallCredentials $cred2_obj): ChannelCredentials;

  /**
   * Create insecure channel credentials
   * @return null
   */
  <<__Native>>
  public function createInsecure(): void;

}
