<?php

/**
 * @generate-class-entries
 * @generate-function-entries
 * @generate-legacy-arginfo
 */

namespace Grpc;

class ChannelCredentials
{

  /**
   * Set default roots pem.
   *
   * @param string $pem_roots PEM encoding of the server root certificates
   *
   * @return void
   */
  public static function setDefaultRootsPem(string $pem_roots): void
  {

  }

  /**
   * if default roots pem is set
   *
   * @return bool
   */
  public static function isDefaultRootsPemSet(): bool
  {

  }

  /**
   * free default roots pem, if it is set
   */
  public static function invalidateDefaultRootsPem(): void
  {

  }

  /**
   * Create a default channel credentials object.
   *
   * @return ChannelCredentials The new default channel credentials object
   */
  public static function createDefault(): ChannelCredentials
  {

  }

  /**
   * Create SSL credentials.
   *
   * @param string|null $pem_root_certs           = null PEM encoding of the server root certificates (optional)
   * @param string|null $pem_private_key              = null PEM encoding of the client's
   *                                              private key (optional)
   * @param string|null $pem_cert_chain               = null PEM encoding of the client's
   *                                              certificate chain (optional)
   *
   * @return ChannelCredentials The new SSL credentials object
   */
  public static function createSsl(
    ?string $pem_root_certs = null,
    ?string $pem_private_key = null,
    ?string $pem_cert_chain = null
  ): ChannelCredentials {

  }

  /**
   * Create composite credentials from two existing credentials.
   *
   * @param ChannelCredentials $channel_creds The first credential
   * @param CallCredentials    $call_creds The second credential
   *
   * @return ChannelCredentials The new composite credentials object
   */
  public static function createComposite(ChannelCredentials $channel_creds, CallCredentials $call_creds): ChannelCredentials
  {

  }

  /**
   * Create insecure channel credentials
   *
   * @return null
   */
  public static function createInsecure()
  {

  }

  /**
   * Create XDS channel credentials
   *
   * @param ChannelCredentials $fallback_creds The fallback credentials used
   *                                           if the channel target does not have the 'xds:///' scheme or if the xDS
   *                                           control plane does not provide information on how to fetch credentials
   *                                           dynamically.
   *
   * @return ChannelCredentials The xDS channel credentials object
   */
  public static function createXds(ChannelCredentials $fallback_creds): ChannelCredentials
  {

  }
}
