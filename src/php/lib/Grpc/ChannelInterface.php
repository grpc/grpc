<?php

namespace Grpc;

interface ChannelInterface
{
    /**
   * * Construct an instance of the Channel class. If the $args array contains a
   * "credentials" key mapping to a ChannelCredentials object, a secure channel
   * will be created with those credentials.
   *
   * @param string $target The hostname to associate with this channel
   * @param array  $args   The arguments to pass to the Channel (optional)
   *
   * @throws \InvalidArgumentException
   */
  public function __construct($target, $args = array());
  /**
   * Get the endpoint this call/stream is connected to
   *
   * @return string The URI of the endpoint
   */
  public function getTarget();
  /**
   * Get the connectivity state of the channel
   *
   * @param bool $try_to_connect try to connect on the channel
   *
   * @return int The grpc connectivity state
   * @throws \InvalidArgumentException
   */
  public function getConnectivityState($try_to_connect = false);
  /**
   * Watch the connectivity state of the channel until it changed
   *
   * @param int     $last_state   The previous connectivity state of the channel
   * @param Timeval $deadline_obj The deadline this function should wait until
   *
   * @return bool If the connectivity state changes from last_state
   *              before deadline
   * @throws \InvalidArgumentException
   */
  public function watchConnectivityState($last_state, Timeval $deadline_obj);
  /**
   * Close the channel
   */
  public function close();
}
