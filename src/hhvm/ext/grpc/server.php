<?hh

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
