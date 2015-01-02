<?php
namespace Grpc;
require_once realpath(dirname(__FILE__) . '/../autoload.php');

/**
 * Represents an active call that sends a stream of messages and then gets a
 * single response.
 */
class ClientStreamingSurfaceActiveCall extends AbstractSurfaceActiveCall {
  /**
   * Create a new simple (single request/single response) active call.
   * @param Channel $channel The channel to communicate on
   * @param string $method The method to call on the remote server
   * @param callable $deserialize The function to deserialize a value
   * @param Traversable $arg_iter The iterator of arguments to send
   * @param array $metadata Metadata to send with the call, if applicable
   */
  public function __construct(Channel $channel,
                              $method,
                              callable $deserialize,
                              $arg_iter,
                              $metadata = array()) {
    parent::__construct($channel, $method, $deserialize, $metadata, 0);
    foreach($arg_iter as $arg) {
      $this->_write($arg);
    }
    $this->_writesDone();
  }

  /**
   * Wait for the server to respond with data and a status
   * @return [response data, status]
   */
  public function wait() {
    $response = $this->_read();
    $status = $this->_getStatus();
    return array($response, $status);
  }
}
