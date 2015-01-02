<?php
namespace Grpc;

require_once realpath(dirname(__FILE__) . '/../autoload.php');

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