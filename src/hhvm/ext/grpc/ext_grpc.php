<?hh

namespace Grpc {

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

<<__NativeData("CallCredentials")>>
class CallCredentials {

  /**
   * Create composite credentials from two existing credentials.
   * @param CallCredentials $cred1_obj The first credential
   * @param CallCredentials $cred2_obj The second credential
   * @return CallCredentials The new composite credentials object
   */
  <<__Native>>
  public static function createComposite(CallCredentials $cred1, CallCredentials $cred2): CallCredentials;

  /**
   * Create a call credentials object from the plugin API
   * @param function $fci The callback function
   * @return CallCredentials The new call credentials object
   */
  <<__Native>>
  public static function createFromPlugin((function(string, string): array) $callback): CallCredentials;

}

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

<<__NativeData("ChannelCredentials")>>
class ChannelCredentials {

  /**
   * Set default roots pem.
   * @param string $pem_roots PEM encoding of the server root certificates
   * @return void
   */
  <<__Native>>
  public static function setDefaultRootsPem(string $pem_roots): void;

  /**
   * Create a default channel credentials object.
   * @return ChannelCredentials The new default channel credentials object
   */
  <<__Native>>
  public static function createDefault(): ChannelCredentials;

  /**
   * Create SSL credentials.
   * @param string $pem_root_certs PEM encoding of the server root certificates
   * @param string $pem_key_cert_pair.private_key PEM encoding of the client's
   *                                              private key (optional)
   * @param string $pem_key_cert_pair.cert_chain PEM encoding of the client's
   *                                             certificate chain (optional)
   * @return ChannelCredentials The new SSL credentials object
   */
  <<__Native>>
  public static function createSsl(?string $pem_root_certs = null,
    ?string $pem_key_cert_pair__private_key = null,
    ?string $pem_key_cert_pair__cert_chain = null): ChannelCredentials;

  /**
   * Create composite credentials from two existing credentials.
   * @param ChannelCredentials $cred1_obj The first credential
   * @param CallCredentials $cred2_obj The second credential
   * @return ChannelCredentials The new composite credentials object
   */
  <<__Native>>
  public static function createComposite(ChannelCredentials $cred1_obj, CallCredentials $cred2_obj): ChannelCredentials;

  /**
   * Create insecure channel credentials
   * @return null
   */
  <<__Native>>
  public static function createInsecure(): ?mixed;

}

<<__NativeData("Server")>>
class Server {

  /**
   * Constructs a new instance of the Server class
   * @param array $args_array The arguments to pass to the server (optional)
   */
  <<__Native>>
  public function __construct(?array $args = null): void;

  /**
   * Request a call on a server. Creates a single GRPC_SERVER_RPC_NEW event.
   * @return object (result object)
   */
  <<__Native>>
  public function requestCall(): object;

  /**
   * Add a http2 over tcp listener.
   * @param string $addr The address to add
   * @return bool True on success, false on failure
   */
  <<__Native>>
  public function addHttp2Port(string $addr): bool;

  /**
   * Add a secure http2 over tcp listener.
   * @param string $addr The address to add
   * @param ServerCredentials The ServerCredentials object
   * @return bool True on success, false on failure
   */
  <<__Native>>
  public function addSecureHttp2Port(string $addr, serverCredentials $serverCredentials): bool;

  /**
   * Start a server - tells all listeners to start listening
   * @return void
   */
  <<__Native>>
  public function start(): void;

}

<<__NativeData("ServerCredentials")>>
class ServerCredentials {

  /**
   * Create SSL credentials.
   * @param string pem_root_certs PEM encoding of the server root certificates
   * @param string pem_private_key PEM encoding of the client's private key
   * @param string pem_cert_chain PEM encoding of the client's certificate chain
   * @return Credentials The new SSL credentials object
   */
  <<__Native>>
  public static function createSsl(string $pem_root_certs, string $pem_private_key, string $pem_cert_chain): object;

}

<<__NativeData("Timeval")>>
class Timeval {

  /**
   * Constructs a new instance of the Timeval class
   * @param long $microseconds The number of microseconds in the interval
   */
  <<__Native>>
  public function __construct(int $microseconds): void;

  /**
   * Adds another Timeval to this one and returns the sum. Calculations saturate
   * at infinities.
   * @param Timeval $other_obj The other Timeval object to add
   * @return Timeval A new Timeval object containing the sum
   */
  <<__Native>>
  public function add(Timeval $otherTimeval): Timeval;

  /**
   * Subtracts another Timeval from this one and returns the difference.
   * Calculations saturate at infinities.
   * @param Timeval $other_obj The other Timeval object to subtract
   * @return Timeval A new Timeval object containing the diff 
   */
  <<__Native>>
  public function subtract(Timeval $otherTimeval): Timeval;

  /**
   * Return negative, 0, or positive according to whether a < b, a == b,
   * or a > b respectively.
   * @param Timeval $a_obj The first time to compare
   * @param Timeval $b_obj The second time to compare
   * @return long
   */
  <<__Native>>
  public static function compare(Timeval $timevalA, Timeval $timevalB): int;

  /**
   * Checks whether the two times are within $threshold of each other
   * @param Timeval $a_obj The first time to compare
   * @param Timeval $b_obj The second time to compare
   * @param Timeval $thresh_obj The threshold to check against
   * @return bool True if $a and $b are within $threshold, False otherwise
   */
  <<__Native>>
  public static function similar(Timeval $timevalA, Timeval $timevalB, Timeval $thresholdTimeval): bool;

  /**
   * Returns the current time as a timeval object
   * @return Timeval The current time
   */
  <<__Native>>
  public static function now(): Timeval;

  /**
   * Returns the zero time interval as a timeval object
   * @return Timeval Zero length time interval
   */
  <<__Native>>
  public static function zero(): Timeval;

  /**
   * Returns the infinite future time value as a timeval object
   * @return Timeval Infinite future time value
   */
  <<__Native>>
  public static function infFuture(): Timeval;

  /**
   * Returns the infinite past time value as a timeval object
   * @return Timeval Infinite past time value
   */
  <<__Native>>
  public static function infPast(): Timeval;

  /**
   * Sleep until this time, interpreted as an absolute timeout
   * @return void
   */
  <<__Native>>
  public function sleepUntil(): void;
}

}
