<?php
namespace Grpc;
require_once realpath(dirname(__FILE__) . '/../autoload.php');

/**
 * Base class for generated client stubs. Stub methods are expected to call
 * _simpleRequest or _streamRequest and return the result.
 */
class BaseStub {

  private $channel;

  public function __construct($hostname, $opts) {
    $this->channel = new Channel($hostname, $opts);
  }

  /**
   * Close the communication channel associated with this stub
   */
  public function close() {
    $channel->close();
  }

  /* This class is intended to be subclassed by generated code, so all functions
     begin with "_" to avoid name collisions. */

  /**
   * Call a remote method that takes a single argument and has a single output
   *
   * @param string $method The name of the method to call
   * @param $argument The argument to the method
   * @param callable $deserialize A function that deserializes the response
   * @param array $metadata A metadata map to send to the server
   * @return SimpleSurfaceActiveCall The active call object
   */
  public function _simpleRequest($method,
                                 $argument,
                                 callable $deserialize,
                                 $metadata = array()) {
    return new SimpleSurfaceActiveCall($this->channel,
                                       $method,
                                       $deserialize,
                                       $argument,
                                       $metadata);
  }

  /**
   * Call a remote method that takes a stream of arguments and has a single
   * output
   *
   * @param string $method The name of the method to call
   * @param $arguments An array or Traversable of arguments to stream to the
   *     server
   * @param callable $deserialize A function that deserializes the response
   * @param array $metadata A metadata map to send to the server
   * @return ClientStreamingSurfaceActiveCall The active call object
   */
  public function _clientStreamRequest($method,
                                       $arguments,
                                       callable $deserialize,
                                       $metadata = array()) {
    return new ClientStreamingSurfaceActiveCall($this->channel,
                                                $method,
                                                $deserialize,
                                                $arguments,
                                                $metadata);
  }

  /**
   * Call a remote method that takes a single argument and returns a stream of
   * responses
   *
   * @param string $method The name of the method to call
   * @param $argument The argument to the method
   * @param callable $deserialize A function that deserializes the responses
   * @param array $metadata A metadata map to send to the server
   * @return ServerStreamingSurfaceActiveCall The active call object
   */
  public function _serverStreamRequest($method,
                                       $argument,
                                       callable $deserialize,
                                       $metadata = array()) {
    return new ServerStreamingSurfaceActiveCall($this->channel,
                                                $method,
                                                $deserialize,
                                                $argument,
                                                $metadata);
  }

  /**
   * Call a remote method with messages streaming in both directions
   *
   * @param string $method The name of the method to call
   * @param callable $deserialize A function that deserializes the responses
   * @param array $metadata A metadata map to send to the server
   * @return BidiStreamingSurfaceActiveCall The active call object
   */
  public function _bidiRequest($method,
                               callable $deserialize,
                               $metadata = array()) {
    return new BidiStreamingSurfaceActiveCall($this->channel,
                                              $method,
                                              $deserialize,
                                              $metadata);
  }
}
