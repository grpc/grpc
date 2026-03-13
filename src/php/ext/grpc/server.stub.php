<?php

/**
 * @generate-class-entries
 * @generate-function-entries
 * @generate-legacy-arginfo
 */

namespace Grpc;

use stdClass;

class Server
{

  /**
   * Constructs a new instance of the Server class
   *
   * @todo better description for $args
   *
   * @param array $args The arguments to pass to the server (optional)
   */
  public function __construct(array $args = [])
  {

  }

  /**
   * Request a call on a server. Creates a single GRPC_SERVER_RPC_NEW event.
   *
   * @return stdClass&object{metadata: array<string, string[]>, host: string, method: string, absolute_deadline:Timeval, call: Call}
   */
  public function requestCall(): stdClass
  {

  }

  /**
   * Add a http2 over tcp listener.
   *
   * @param string $addr The address to add
   *
   * @return int Port on success, 0 on failure
   */
  public function addHttp2Port(string $addr): int
  {

  }

  /**
   * Add a secure http2 over tcp listener.
   *
   * @param string            $addr  The address to add
   * @param ServerCredentials $server_creds The ServerCredentials object
   *
   * @return int Port on success, 0 on failure
   */
  public function addSecureHttp2Port(string $addr, ServerCredentials $server_creds): int
  {

  }

  /**
   * Start a server - tells all listeners to start listening
   *
   * @return void
   */
  public function start(): void
  {

  }
}
