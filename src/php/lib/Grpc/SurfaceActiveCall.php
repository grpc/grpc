<?php
namespace Grpc;

/**
 * Represents an active call that allows sending and recieving messages.
 * Subclasses restrict how data can be sent and recieved.
 */
abstract class AbstractSurfaceActiveCall {
  private $active_call;
  private $deserialize;

  /**
   * Create a new surface active call.
   * @param Channel $channel The channel to communicate on
   * @param string $method The method to call on the remote server
   * @param callable $deserialize The function to deserialize a value
   * @param array $metadata Metadata to send with the call, if applicable
   * @param long $flags Write flags to use with this call
   */
  public function __construct(Channel $channel,
                       $method,
                       callable $deserialize,
                       $metadata = array(),
                       $flags = 0) {
    $this->active_call = new ActiveCall($channel, $method, $metadata, $flags);
    $this->deserialize = $deserialize;
  }

  /**
   * @return The metadata sent by the server
   */
  public function getMetadata() {
    return $this->metadata();
  }

  /**
   * Cancels the call
   */
  public function cancel() {
    $this->active_call->cancel();
  }

  protected function _read() {
    $response = $this->active_call->read();
    if ($response == null) {
      return null;
    }
    return call_user_func($this->deserialize, $response);
  }

  protected function _write($value) {
    return $this->active_call->write($value->serialize());
  }

  protected function _writesDone() {
    $this->active_call->writesDone();
  }

  protected function _getStatus() {
    return $this->active_call->getStatus();
  }
}

/**
 * Represents an active call that sends a single message and then gets a single
 * response.
 */
class SimpleSurfaceActiveCall extends AbstractSurfaceActiveCall {
  /**
   * Create a new simple (single request/single response) active call.
   * @param Channel $channel The channel to communicate on
   * @param string $method The method to call on the remote server
   * @param callable $deserialize The function to deserialize a value
   * @param $arg The argument to send
   * @param array $metadata Metadata to send with the call, if applicable
   */
  public function __construct(Channel $channel,
                              $method,
                              callable $deserialize,
                              $arg,
                              $metadata = array()) {
    parent::__construct($channel, $method, $deserialize, $metadata,
                        \Grpc\WRITE_BUFFER_HINT);
    $this->_write($arg);
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

/**
 * Represents an active call that sends a single message and then gets a stream
 * of reponses
 */
class ServerStreamingSurfaceActiveCall extends AbstractSurfaceActiveCall {
  /**
   * Create a new simple (single request/single response) active call.
   * @param Channel $channel The channel to communicate on
   * @param string $method The method to call on the remote server
   * @param callable $deserialize The function to deserialize a value
   * @param $arg The argument to send
   * @param array $metadata Metadata to send with the call, if applicable
   */
  public function __construct(Channel $channel,
                              $method,
                              callable $deserialize,
                              $arg,
                              $metadata = array()) {
    parent::__construct($channel, $method, $deserialize, $metadata,
                        \Grpc\WRITE_BUFFER_HINT);
    $this->_write($arg);
    $this->_writesDone();
  }

  /**
   * @return An iterator of response values
   */
  public function responses() {
    while(($response = $this->_read()) != null) {
      yield $response;
    }
  }

  public function getStatus() {
    return $this->_getStatus();
  }
}

/**
 * Represents an active call that allows for sending and recieving messages in
 * streams in any order.
 */
class BidiStreamingSurfaceActiveCall extends AbstractSurfaceActiveCall {

  /**
   * Reads the next value from the server.
   * @return The next value from the server, or null if there is none
   */
  public function read() {
    return $this->_read();
  }

  /**
   * Writes a single message to the server. This cannot be called after
   * writesDone is called.
   * @param $value The message to send
   */
  public function write($value) {
    $this->_write($value);
  }

  /**
   * Indicate that no more writes will be sent
   */
  public function writesDone() {
    $this->_writesDone();
  }

  /**
   * Wait for the server to send the status, and return it.
   * @return object The status object, with integer $code and string $details
   *     members
   */
  public function getStatus() {
    return $this->_getStatus();
  }
}