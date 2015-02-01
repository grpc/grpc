<?php
namespace Grpc;
require_once realpath(dirname(__FILE__) . '/../autoload.php');

/**
 * Represents an active call that allows sending and recieving binary data
 */
class ActiveCall {
  private $completion_queue;
  private $call;
  private $flags;
  private $metadata;

  /**
   * Create a new active call.
   * @param Channel $channel The channel to communicate on
   * @param string $method The method to call on the remote server
   * @param array $metadata Metadata to send with the call, if applicable
   * @param long $flags Write flags to use with this call
   */
  public function __construct(Channel $channel,
                              $method,
                              $metadata = array(),
                              $flags = 0) {
    $this->completion_queue = new CompletionQueue();
    $this->call = new Call($channel, $method, Timeval::inf_future());
    $this->call->add_metadata($metadata, 0);
    $this->flags = $flags;

    // Invoke the call.
    $this->call->invoke($this->completion_queue,
                        CLIENT_METADATA_READ,
                        FINISHED, 0);
    $metadata_event = $this->completion_queue->pluck(CLIENT_METADATA_READ,
                                                     Timeval::inf_future());
    $this->metadata = $metadata_event->data;
  }

  /**
   * @return The metadata sent by the server.
   */
  public function getMetadata() {
    return $this->metadata;
  }

  /**
   * Cancels the call
   */
  public function cancel() {
    $this->call->cancel();
  }

  /**
   * Read a single message from the server.
   * @return The next message from the server, or null if there is none.
   */
  public function read() {
    $this->call->start_read(READ);
    $read_event = $this->completion_queue->pluck(READ, Timeval::inf_future());
    return $read_event->data;
  }

  /**
   * Write a single message to the server. This cannot be called after
   * writesDone is called.
   * @param ByteBuffer $data The data to write
   */
  public function write($data) {
    if($this->call->start_write($data,
                                WRITE_ACCEPTED,
                                $this->flags) != OP_OK) {
      // TODO(mlumish): more useful error
      throw new \Exception("Cannot call write after writesDone");
    }
    $this->completion_queue->pluck(WRITE_ACCEPTED, Timeval::inf_future());
  }

  /**
   * Indicate that no more writes will be sent.
   */
  public function writesDone() {
    $this->call->writes_done(FINISH_ACCEPTED);
    $this->completion_queue->pluck(FINISH_ACCEPTED, Timeval::inf_future());
  }

  /**
   * Wait for the server to send the status, and return it.
   * @return object The status object, with integer $code, string $details,
   *     and array $metadata members
   */
  public function getStatus() {
    $status_event = $this->completion_queue->pluck(FINISHED,
                                                   Timeval::inf_future());
    return $status_event->data;
  }
}