declare module 'grpc' {
  import { ReflectionObject, Service } from 'protobufjs'

  /**
   * Default options for loading proto files into gRPC
   */
  export interface ILoadOptions {
    /**
     * Load this file with field names in camel case instead of their original case. Defaults to `false`.
     */
    convertFieldsToCamelCase: boolean;

    /**
     * Deserialize bytes values as base64 strings instead of buffers. Defaults to `false`.
     */
    binaryAsBase64: boolean;

    /**
     * Deserialize long values as strings instead of objects. Defaults to `true`.
     */
    longsAsStrings: boolean;

    /**
     * Deserialize enum values as strings instead of numbers. Defaults to `true`.
     */
    enumsAsStrings: boolean;
  }

  export interface ILoadProtobufOptions extends ILoadOptions {
    /**
     * Indicate that an object from the corresponding version of
     * ProtoBuf.js is provided in the value argument. If the option is 'detect',
     * gRPC will guess what the version is based on the structure of the value.
     * Defaults to 'detect'.
     */
    protobufjsVersion: 5 | 6 | 'detect';
  }

  /**
   * Load a gRPC object from a `.proto` file.
   * @param filename The file to load
   * @param format The file format to expect. Defaults to 'proto'.
   * @param options Options to apply to the loaded file.
   * @return The resulting gRPC object.
   */
  export function load(filename: string, format?: 'proto' | 'json', options?: ILoadOptions): IProtobufDefinition;

  /**
   * Map from `.proto` file.
   * - Namespaces become maps from the names of their direct members to those member objects
   * - Service definitions become client constructors for clients for that service. They also have a service member that can be used for constructing servers.
   * - Message definitions become Message constructors like those that ProtoBuf.js would create
   * - Enum definitions become Enum objects like those that ProtoBuf.js would create
   * - Anything else becomes the relevant reflection object that ProtoBuf.js would create
   */
  export interface IProtobufDefinition {
    [key: string]: any;
  }

  /**
   * Load a ProtoBuf.js object as a gRPC object.
   * - protobufjsVersion: Available values are 5, 6, and 'detect'. 5 and 6
   *   respectively indicate that an object from the corresponding version of
   *   ProtoBuf.js is provided in the value argument. If the option is 'detect',
   *   gRPC will guess what the version is based on the structure of the value.
   *   Defaults to 'detect'.
   * @param {Object} value The ProtoBuf.js reflection object to load
   * @param {Object=} options Options to apply to the loaded file
   * @return {Object<string, *>} The resulting gRPC object
   */
  export function loadObject(replectionObject: ReflectionObject, options?: ILoadProtobufOptions): IProtobufDefinition;

  /**
   * Serer object that stores request handlers and delegates incoming requests to those handlers
   */
  export class Server {
    /**
     * Constructs a server object
     * @param options Options that should be passed to the internal server implementation
     */
    constructor(options?: IServerOptions);

    /**
     * Start the server and begin handling requests
     */
    start(): void;

    /**
     * Gracefully shuts down the server. The server will stop receiving new calls,
     * and any pending calls will complete. The callback will be called when all
     * pending calls have completed and the server is fully shut down. This method
     * is idempotent with itself and forceShutdown.
     * @param callback The shutdown complete callback
     */
    tryShutdown(callback: () => void): void;

    /**
     * Forcibly shuts down the server. The server will stop receiving new calls
     * and cancel all pending calls. When it returns, the server has shut down.
     * This method is idempotent with itself and tryShutdown, and it will trigger
     * any outstanding tryShutdown callbacks.
     */
    forceShutdown(): void;

    /**
     * Registers a handler to handle the named method. Fails if there already is
     * a handler for the given method. Returns true on success
     * @param name The name of the method that the provided function should handle/respond to.
     * @param handler Function that takes a stream of request values and returns a stream of response values
     * @param serialize Serialization function for responses
     * @param deserialize Deserialization function for requests
     * @param type The streaming type of method that this handles
     * @return True if the handler was set. False if a handler was already set for that name.
     */
    register(name: string, handler: () => void, serialize: (obj: any) => Buffer, deserialize: (buffer: Buffer) => any, type: string): boolean;

    /**
     * Add a service to the server, with a corresponding implementation. If you are
     * generating this from a proto file, you should instead use `addProtoService`.
     * @param service The service descriptor, as `getProtobufServiceAttrs` returns
     * @param implementation Map of method names to method implementation for the provided service.
     */
    addService(service: { [index: string]: any }, implementation: { [index: string]: () => void }): void;

    /**
     * Add a proto service to the server, with a corresponding implementation
     * @deprecated Use grpc.load and Server#addService instead
     * @param service The proto service descriptor
     * @param implementation Map of method names to method implementation for the provided service.
     */
    addProtoService(service: Service, implementation: any): void;

    /**
     * Binds the server to the given port, with SSL enabled if creds is given
     * @param port The port that the server should bind on, in the format "address:port"
     * @param creds Server credential object to be used for SSL. Pass an insecure credentials object for an insecure port.
     */
    bind(port: string, creds: ServerCredentials): any;
  }

  export interface IServerOptions {

  }

  export interface IError {
    code: string;
    message: string;
  }

  /**
   * Credentials factories
   */
  export const credentials: {
    /**
     * Create an SSL Credentials object. If using a client-side certificate, both
     * the second and third arguments must be passed.
     * @param rootCerts The root certificate data
     * @param privateKey The client certificate private key, if applicable
     * @param certChain The client certificate cert chain, if applicable
     * @return The SSL Credentials object
     */
    createSsl(rootCerts: Buffer, privateKey?: Buffer, certChain?: Buffer): ChannelCredentials;

    /**
     * Create a gRPC credentials object from a metadata generation function. This
     * function gets the service URL and a callback as parameters. The error
     * passed to the callback can optionally have a 'code' value attached to it,
     * which corresponds to a status code that this library uses.
     * @param metadataGenerator The function that generates metadata
     * @return The credentials object
     */
    createFromMetadataGenerator(metadataGenerator: (s: string, callback: (error: Error, metadata: Metadata) => void) => void): CallCredentials;

    /**
     * Create a gRPC credential from a Google credential object.
     * @param googleCredential The Google credential object to use
     * @return The resulting credentials object
     */
    createFromGoogleCredential(googleCredential: object): CallCredentials;

    /**
     * Combine a ChannelCredentials with any number of CallCredentials into a single
     * ChannelCredentials object.
     * @param channelCredential The ChannelCredentials to start with
     * @param credentials The CallCredentials to compose
     * @return A credentials object that combines all of the input credentials
     */
    combineChannelCredentials(channelCredential: ChannelCredentials, ...credentials: CallCredentials[]): ChannelCredentials;

    /**
     * Combine any number of CallCredentials into a single CallCredentials object
     * @param {...CallCredentials} credentials the CallCredentials to compose
     * @return CallCredentials A credentials object that combines all of the input
     *     credentials
     */
    combineCallCredentials(...credentials: CallCredentials[]): CallCredentials;

    /**
     * Create an insecure credentials object. This is used to create a channel that
     * does not use SSL. This cannot be composed with anything.
     * @return The insecure credentials object
     */
    createInsecure(): ChannelCredentials;
  }

  export class ChannelCredentials {
    static Init(exports: object): void;
    static HasInstance(val: any): boolean;
  }

  export class CallCredentials {
    static Init(exports: object): void;
    static HasInstance(val: any): boolean;
  }

  /**
   * ServerCredentials factories
   */
  export class ServerCredentials {
    static createInsecure(): ServerCredentials;
  }


  /**
   * Sets the logger function for the gRPC module. For debugging purposes, the C
   * core will log synchronously directly to stdout unless this function is
   * called. Note: the output format here is intended to be informational, and
   * is not guaranteed to stay the same in the future.
   * Logs will be directed to logger.error.
   * @param logger A Console-like object.
   */
  export function setLogger(logger: { error: (message: string) => void }): void;

  /**
   * Sets the logger verbosity for gRPC module logging. The options are members
   * of the grpc.logVerbosity map.
   * @param verbosity The minimum severity to log
   */
  export function setLogVerbosity(verbosity: number): void

  export class Metadata {
    /**
     * Class for storing metadata. Keys are normalized to lowercase ASCII.
     */
    constructor();

    /**
     * Sets the given value for the given key, replacing any other values associated
     * with that key. Normalizes the key.
     * @param key The key to set
     * @param value The value to set. Must be a buffer if and only if the normalized key ends with '-bin'
     */
    set(key: string, value: string | Buffer): void;

    /**
     * Adds the given value for the given key. Normalizes the key.
     * @param key The key to add to.
     * @param value The value to add. Must be a buffer if and only if the normalized key ends with '-bin'
     */
    add(key: string, value: string | Buffer): void;

    /**
     * Remove the given key and any associated values. Normalizes the key.
     * @param key The key to remove
     */
    remove(key: string): void;

    /**
     * Gets a list of all values associated with the key. Normalizes the key.
     * @param key The key to get
     * @return The values associated with that key
     */
    get(key: string): (string | Buffer)[];

    /**
     * Get a map of each key to a single associated value. This reflects the most
     * common way that people will want to see metadata.
     * @return A key/value mapping of the metadata
     */
    getMap(): { [index: string]: string | Buffer };

    /**
     * Clone the metadata object.
     * @return The new cloned object
     */
    clone(): Metadata;
  }


  /**
   * Status name to code number mapping
   */
  export enum status {
    /**
     * Not an error; returned on success
     */
    OK = 0,

    /**
     * The operation was cancelled (typically by the caller).
     */
    CANCELLED = 1,

    /**
     * Unknown error.  An example of where this error may be returned is
     * if a Status value received from another address space belongs to
     * an error-space that is not known in this address space.  Also
     * errors raised by APIs that do not return enough error information
     * may be converted to this error.
     */
    UNKNOWN = 2,

    /**
     * Client specified an invalid argument.  Note that this differs
     * from FAILED_PRECONDITION.  INVALID_ARGUMENT indicates arguments
     * that are problematic regardless of the state of the system
     * (e.g., a malformed file name).
     */
    INVALID_ARGUMENT = 3,

    /**
     * Deadline expired before operation could complete.  For operations
     * that change the state of the system, this error may be returned
     * even if the operation has completed successfully.  For example, a
     * successful response from a server could have been delayed long
     * enough for the deadline to expire.
     */
    DEADLINE_EXCEEDED = 4,

    /**
     * Some requested entity (e.g., file or directory) was not found.
     */
    NOT_FOUND = 5,

    /**
     * Some entity that we attempted to create (e.g., file or directory)
     * already exists.
     */
    ALREADY_EXISTS = 6,

    /**
     * The caller does not have permission to execute the specified
     * operation.  PERMISSION_DENIED must not be used for rejections
     * caused by exhausting some resource (use RESOURCE_EXHAUSTED
     * instead for those errors).  PERMISSION_DENIED must not be
     * used if the caller can not be identified (use UNAUTHENTICATED
     * instead for those errors).
     */
    PERMISSION_DENIED = 7,

    /**
     * The request does not have valid authentication credentials for the
     * operation.
     */
    UNAUTHENTICATED = 16,

    /* Some resource has been exhausted, perhaps a per-user quota, or
       perhaps the entire file system is out of space. */
    RESOURCE_EXHAUSTED = 8,

    /* Operation was rejected because the system is not in a state
       required for the operation's execution.  For example, directory
       to be deleted may be non-empty, an rmdir operation is applied to
       a non-directory, etc.

       A litmus test that may help a service implementor in deciding
       between FAILED_PRECONDITION, ABORTED, and UNAVAILABLE:
        (a) Use UNAVAILABLE if the client can retry just the failing call.
        (b) Use ABORTED if the client should retry at a higher-level
            (e.g., restarting a read-modify-write sequence).
        (c) Use FAILED_PRECONDITION if the client should not retry until
            the system state has been explicitly fixed.  E.g., if an "rmdir"
            fails because the directory is non-empty, FAILED_PRECONDITION
            should be returned since the client should not retry unless
            they have first fixed up the directory by deleting files from it.
        (d) Use FAILED_PRECONDITION if the client performs conditional
            REST Get/Update/Delete on a resource and the resource on the
            server does not match the condition. E.g., conflicting
            read-modify-write on the same resource. */
    FAILED_PRECONDITION = 9,

    /* The operation was aborted, typically due to a concurrency issue
       like sequencer check failures, transaction aborts, etc.

       See litmus test above for deciding between FAILED_PRECONDITION,
       ABORTED, and UNAVAILABLE. */
    ABORTED = 10,

    /* Operation was attempted past the valid range.  E.g., seeking or
       reading past end of file.

       Unlike INVALID_ARGUMENT, this error indicates a problem that may
       be fixed if the system state changes. For example, a 32-bit file
       system will generate INVALID_ARGUMENT if asked to read at an
       offset that is not in the range [0,2^32-1], but it will generate
       OUT_OF_RANGE if asked to read from an offset past the current
       file size.

       There is a fair bit of overlap between FAILED_PRECONDITION and
       OUT_OF_RANGE.  We recommend using OUT_OF_RANGE (the more specific
       error) when it applies so that callers who are iterating through
       a space can easily look for an OUT_OF_RANGE error to detect when
       they are done. */
    OUT_OF_RANGE = 11,

    /* Operation is not implemented or not supported/enabled in this service. */
    UNIMPLEMENTED = 12,

    /* Internal errors.  Means some invariants expected by underlying
       system has been broken.  If you see one of these errors,
       something is very broken. */
    INTERNAL = 13,

    /* The service is currently unavailable.  This is a most likely a
       transient condition and may be corrected by retrying with
       a backoff.

       See litmus test above for deciding between FAILED_PRECONDITION,
       ABORTED, and UNAVAILABLE. */
    UNAVAILABLE = 14,

    /* Unrecoverable data loss or corruption. */
    DATA_LOSS = 15,

    /* Force users to include a default branch: */
    _DO_NOT_USE = -1
  }

  /**
   * Propagate flag name to number mapping
   */
  export const propagate: {}

  /**
   * Call error name to code number mapping
   */
  export const callError: {}

  /**
   * Write flag name to code number mapping
   */
  export const writeFlags: {}

  /**
   * Log verbosity setting name to code number mapping
   */
  export const logVerbosity: {}

  /**
   * Creates a constructor for a client with the given methods.
   * @param methods An object mapping method names to method attributes
   * @param serviceName The fully qualified name of the service
   * @return New client constructor
   */
  export function makeGenericClientConstructor(methods: IMethodsMap, serviceName: string): Client;

  /**
   * Create a client with the given methods
   */
  export class Client {
    /**
     * Create an instance of Client
     * @param address The address of the server to connect to
     * @param credentials Credentials to use to connect to the server
     * @param options Options to pass to the underlying channel
     */
    constructor(address: string, credentials: any, options: IClientOptions)
  }

  export interface IClientOptions {

  }

  /**
   * The methods object map.
   */
  export interface IMethodsMap {
    [index: string]: {
      /**
       * The path on the server for accessing the method. For example, for protocol buffers, we use "/service_name/method_name"
       */
      path: string;

      /**
       * Indicating whether the client sends a stream
       */
      requestStream: boolean;

      /**
       * Indicating whether the server sends a stream
       */
      responseStream: boolean;

      /**
       * Function to serialize request objects
       */
      requestSerialize: () => any;

      /**
       * Function to deserialize response objects
       */
      responseDeserialize: () => any;
    }
  }

  /**
   * Return the underlying channel object for the specified client
   * @param client
   * @return The channel
   */
  export function getClientChannel(client: Client): any;

  /**
   * Wait for the client to be ready. The callback will be called when the
   * client has successfully connected to the server, and it will be called
   * with an error if the attempt to connect to the server has unrecoverablly
   * failed or if the deadline expires. This function will make the channel
   * start connecting if it has not already done so.
   * @param client The client to wait on
   * @param deadline When to stop waiting for a connection. Pass Infinity to wait forever.
   * @param callback The callback to call when done attempting to connect.
   */
  export function waitForClientReady(client: Client, deadline: Date | number, callback: (error?: Error) => void): void;

  export function closeClient(clientObj: Client): void;
}
