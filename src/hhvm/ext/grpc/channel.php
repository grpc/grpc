<?hh

<<__NativeData("Channel")>>
class Channel {

  /**
   * Construct an instance of the Channel class. If the $args array contains a
   * "credentials" key mapping to a ChannelCredentials object, a secure channel
   * will be created with those credentials.
   * @param string $target The hostname to associate with this channel
   * @param array $args_array The arguments to pass to the Channel
   */
  <<__Native>>
  public function __construct(string $target, array<string, mixed> $args): void;

  /**
   * Get the endpoint this call/stream is connected to
   * @return string The URI of the endpoint
   */
  <<__Native>>
  public function getTarget(): string;

  /**
   * Get the connectivity state of the channel
   * @param bool $try_to_connect Try to connect on the channel (optional)
   * @return long The grpc connectivity state
   */
  <<__Native>>
  public function getConnectivityState(bool $try_to_connect = false): int;

  /**
   * Watch the connectivity state of the channel until it changed
   * @param long $last_state The previous connectivity state of the channel
   * @param Timeval $deadline_obj The deadline this function should wait until
   * @return bool If the connectivity state changes from last_state
   *              before deadline
   */
  <<__Native>>
  public function watchConnectivityState(int $last_state, Timeval $deadline_timeval): bool;

  /**
   * Close the channel
   * @return void
   */
  <<__Native>>
  public function close(): void;

}
