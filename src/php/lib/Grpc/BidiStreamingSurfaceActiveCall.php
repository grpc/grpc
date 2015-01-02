<?php
namespace Grpc;
require_once realpath(dirname(__FILE__) . '/../autoload.php');

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