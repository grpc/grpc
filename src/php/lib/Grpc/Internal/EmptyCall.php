<?php

namespace Grpc\Internal;

/**
 * Class EmptyCall.
 * When ExtensionChannel is used to create the Grpc\Call object, EmptyCall will
 * let the program continue without throwing exception.
 * In addition, EmptyCall contains all information needed for creating a Grpc\Call,
 * including ExtensionChannel object.
 * @package Grpc
 */
class EmptyCall
{
  private $channel;
  private $method;
  private $deserialize;
  private $options;
  private $metadata;
  public function __construct($channel,
                              $method,
                              $deserialize,
                              $options) {
    $this->channel = $channel;
    $this->method = $method;
    $this->deserialize = $deserialize;
    $this->options = $options;
  }
  public function startBatch(array $batch) {
    $this->metadata = $batch[\Grpc\OP_SEND_INITIAL_METADATA];
  }
  public function setCredentials(\Grpc\CallCredentials $creds_obj) {}
  public function getPeer() {}
  public function cancel() {}
  public function _getChannel() {
    return $this->channel;
  }
  public function _getMethod() {
    return $this->method;
  }
  public function _getDeserialize() {
    return $this->deserialize;
  }
  public function _getOptions() {
    return $this->options;
  }
  public function _getMetadata() {
    return $this->metadata;
  }
}
