<?hh

<<__NativeData("Call")>>
class Call {

  /**
   * Constructs a new instance of the Call class.
   * @param Channel $channel_obj The channel to associate the call with.
   *                             Must not be closed.
   * @param string $method The method to call
   * @param Timeval $deadline_obj The deadline for completing the call
   * @param string $host_override The host is set by user (optional)
   */
  <<__Native>>
  public function __construct(Channel $channel, string $method, Timeval $deadlineTimeval, ?string $host_override = null): void;

  /**
   * Start a batch of RPC actions.
   * @param array $array Array of actions to take
   * @return object Object with results of all actions
   */
  <<__Native>>
  public function startBatch(array<int, mixed> $actions): object;

  /**
   * Get the endpoint this call/stream is connected to
   * @return string The URI of the endpoint
   */
  <<__Native>>
  public function getPeer(): string;

  /**
   * Cancel the call. This will cause the call to end with STATUS_CANCELLED
   * if it has not already ended with another status.
   * @return void
   */
  <<__Native>>
  public function cancel(): void;

  /**
   * Set the CallCredentials for this call.
   * @param CallCredentials $creds_obj The CallCredentials object
   * @return int The error code
   */
  <<__Native>>
  public function setCredentials(CallCredentials $credentials): int;
}
