<?php

/**
 * @generate-class-entries
 * @generate-function-entries
 * @generate-legacy-arginfo
 */

namespace Grpc;

class Channel
{
  /**
   * Construct an instance of the Channel class.
   *
   * By default, the underlying grpc_channel is "persistent". That is, given
   * the same set of parameters passed to the constructor, the same underlying
   * grpc_channel will be returned.
   *
   * If the $args array contains a "credentials" key mapping to a
   * ChannelCredentials object, a secure channel will be created with those
   * credentials.
   *
   * If the $args array contains a "force_new" key mapping to a boolean value
   * of "true", a new and separate underlying grpc_channel will be created
   * and returned. This will not affect existing channels.
   *
   * @todo better description for $args
   *
   * @param string $target The hostname to associate with this channel
   * @param array  $args   The arguments to pass to the Channel
   */
  public function __construct(string $target, array $args = [])
  {

  }

  /**
   * Get the endpoint this call/stream is connected to
   *
   * @return string The URI of the endpoint
   */
  public function getTarget(): string
  {
  }

  /**
   * Get the connectivity state of the channel
   *
   * @param bool $try_to_connect Try to connect on the channel (optional)
   *
   * @return int The grpc connectivity state
   */
  public function getConnectivityState(bool $try_to_connect = false): int
  {

  }

  /**
   * Watch the connectivity state of the channel until it changed
   *
   * @param int     $last_state The previous connectivity state of the channel
   * @param Timeval $deadline   The deadline this function should wait until
   *
   * @return bool If the connectivity state changes from last_state
   *              before deadline
   */
  public function watchConnectivityState(int $last_state, Timeval $deadline): bool
  {

  }

  /**
   * Close the channel
   *
   * @return void
   */
  public function close(): void
  {

  }

  /**
   * @return void
   * @internal Test only.
   *
   * Clean all channels in the persistent. Test only.
   */
  public function cleanPersistentList(): void
  {

  }

  /**
   * @return array
   * @internal Test only.
   *
   * Return the info about the current channel. Test only.
   */
  public function getChannelInfo(): array
  {

  }

  /**
   * @internal Test only.
   *
   * Return an array of all channels in the persistent list. Test only.
   * @return array
   */
  public function getPersistentList(): array
  {

  }
}
