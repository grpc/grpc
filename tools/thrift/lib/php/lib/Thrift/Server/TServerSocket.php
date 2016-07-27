<?php

namespace Thrift\Server;

use Thrift\Transport\TSocket;

/**
 * Socket implementation of a server agent.
 *
 * @package thrift.transport
 */
class TServerSocket extends TServerTransport
{
  /**
   * Handle for the listener socket
   *
   * @var resource
   */
  private $listener_;

  /**
   * Port for the listener to listen on
   *
   * @var int
   */
  private $port_;

  /**
   * Timeout when listening for a new client
   *
   * @var int
   */
  private $acceptTimeout_ = 30000;

  /**
   * Host to listen on
   *
   * @var string
   */
  private $host_;

  /**
   * ServerSocket constructor
   *
   * @param string $host        Host to listen on
   * @param int $port           Port to listen on
   * @return void
   */
  public function __construct($host = 'localhost', $port = 9090)
  {
    $this->host_ = $host;
    $this->port_ = $port;
  }

  /**
   * Sets the accept timeout
   *
   * @param int $acceptTimeout
   * @return void
   */
  public function setAcceptTimeout($acceptTimeout)
  {
    $this->acceptTimeout_ = $acceptTimeout;
  }

  /**
   * Opens a new socket server handle
   *
   * @return void
   */
  public function listen()
  {
    $this->listener_ = stream_socket_server('tcp://' . $this->host_ . ':' . $this->port_);
  }

  /**
   * Closes the socket server handle
   *
   * @return void
   */
  public function close()
  {
    @fclose($this->listener_);
    $this->listener_ = null;
  }

  /**
   * Implementation of accept. If not client is accepted in the given time
   *
   * @return TSocket
   */
  protected function acceptImpl()
  {
    $handle = @stream_socket_accept($this->listener_, $this->acceptTimeout_ / 1000.0);
    if(!$handle) return null;

    $socket = new TSocket();
    $socket->setHandle($handle);

    return $socket;
  }
}
