gRPC environment variables
--------------------------

gRPC C core based implementations (those contained in this repository) expose
some configuration as environment variables that can be set.

* grpc_proxy, https_proxy, http_proxy
  The URI of the proxy to use for HTTP CONNECT support. These variables are
  checked in order, and the first one that has a value is used.

* no_grpc_proxy, no_proxy
  A comma separated list of hostnames to connect to without using a proxy even
  if a proxy is set. These variables are checked in order, and the first one
  that has a value is used.

* GRPC_ABORT_ON_LEAKS
  A debugging aid to cause a call to abort() when gRPC objects are leaked past
  grpc_shutdown(). Set to 1 to cause the abort, if unset or 0 it does not
  abort the process.

* GOOGLE_APPLICATION_CREDENTIALS
  The path to find the credentials to use when Google credentials are created

* GRPC_SSL_CIPHER_SUITES
  A colon separated list of cipher suites to use with OpenSSL
  Defaults to:
    ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384

* GRPC_DEFAULT_SSL_ROOTS_FILE_PATH
  PEM file to load SSL roots from

* GRPC_POLL_STRATEGY [posix-style environments only]
  Declares which polling engines to try when starting gRPC.
  This is a comma-separated list of engines, which are tried in priority order
  first -> last.
  Available polling engines include:
  - epoll (linux-only) - a polling engine based around the epoll family of
    system calls
  - poll - a portable polling engine based around poll(), intended to be a
    fallback engine when nothing better exists
  - legacy - the (deprecated) original polling engine for gRPC

* GRPC_TRACE
  A comma separated list of tracers that provide additional insight into how
  gRPC C core is processing requests via debug logs. Available tracers include:
  - api - traces api calls to the C core
  - bdp_estimator - traces behavior of bdp estimation logic
  - call_error - traces the possible errors contributing to final call status
  - cares_resolver - traces operations of the c-ares based DNS resolver
  - cares_address_sorting - traces operations of the c-ares based DNS
    resolver's resolved address sorter
  - cds_lb - traces cds LB policy
  - channel - traces operations on the C core channel stack
  - channel_stack - traces the set of filters in a channel stack upon
    construction
  - client_channel - traces client channel control plane activity, including
    resolver and load balancing policy interaction
  - client_channel_call - traces client channel call activity related to name
    resolution
  - client_channel_lb_call - traces client channel call activity related
    to load balancing picking
  - compression - traces compression operations
  - connectivity_state - traces connectivity state changes to channels
  - cronet - traces state in the cronet transport engine
  - dns_resolver - traces state in the native DNS resolver
  - executor - traces grpc's internal thread pool ('the executor')
  - glb - traces the grpclb load balancer
  - handshaker - traces handshaking state
  - health_check_client - traces health checking client code
  - http - traces state in the http2 transport engine
  - http2_stream_state - traces all http2 stream state mutations.
  - http1 - traces HTTP/1.x operations performed by gRPC
  - inproc - traces the in-process transport
  - http_keepalive - traces gRPC keepalive pings
  - flowctl - traces http2 flow control
  - op_failure - traces error information when failure is pushed onto a
    completion queue
  - pick_first - traces the pick first load balancing policy
  - plugin_credentials - traces plugin credentials
  - pollable_refcount - traces reference counting of 'pollable' objects (only
    in DEBUG)
  - priority_lb - traces priority LB policy
  - resource_quota - trace resource quota objects internals
  - ring_hash_lb - traces the ring hash load balancing policy
  - rls_lb - traces the RLS load balancing policy
  - round_robin - traces the round_robin load balancing policy
  - weighted_round_robin_lb - traces the weighted_round_robin load balancing
    policy
  - queue_pluck
  - grpc_authz_api - traces gRPC authorization
  - server_channel - lightweight trace of significant server channel events
  - secure_endpoint - traces bytes flowing through encrypted channels
  - subchannel - traces the connectivity state of subchannel
  - subchannel_pool - traces subchannel pool
  - timer - timers (alarms) in the grpc internals
  - timer_check - more detailed trace of timer logic in grpc internals
  - transport_security - traces metadata about secure channel establishment
  - tcp - traces bytes in and out of a channel
  - tsi - traces tsi transport security
  - weighted_target_lb - traces weighted_target LB policy
  - xds_client - traces xds client
  - xds_cluster_manager_lb - traces cluster manager LB policy
  - xds_cluster_impl_lb - traces cluster impl LB policy
  - xds_cluster_resolver_lb - traces xds cluster resolver LB policy
  - xds_resolver - traces xds resolver

  The following tracers will only run in binaries built in DEBUG mode. This is
  accomplished by invoking `CONFIG=dbg make <target>`
  - metadata - tracks creation and mutation of metadata
  - combiner - traces combiner lock state
  - call_combiner - traces call combiner state
  - closure - tracks closure creation, scheduling, and completion
  - fd_trace - traces fd create(), shutdown() and close() calls for channel fds.
  - pending_tags - traces still-in-progress tags on completion queues
  - polling - traces the selected polling engine
  - polling_api - traces the api calls to polling engine
  - subchannel_refcount
  - queue_refcount
  - error_refcount
  - stream_refcount
  - slice_refcount
  - workqueue_refcount
  - fd_refcount
  - cq_refcount
  - auth_context_refcount
  - security_connector_refcount
  - resolver_refcount
  - lb_policy_refcount
  - chttp2_refcount

  'all' can additionally be used to turn all traces on.
  Individual traces can be disabled by prefixing them with '-'.

  'refcount' will turn on all of the tracers for refcount debugging.

  if 'list_tracers' is present, then all of the available tracers will be
  printed when the program starts up.

  Example:
  export GRPC_TRACE=all,-pending_tags

* GRPC_VERBOSITY
  Default gRPC logging verbosity - one of:
  - DEBUG - log all gRPC messages
  - INFO - log INFO and ERROR message
  - ERROR - log only errors (default)
  - NONE - won't log any

* GRPC_STACKTRACE_MINLOGLEVEL
  Minimum loglevel to print the stack-trace - one of DEBUG, INFO, ERROR, and NONE.
  NONE is a default value.

* GRPC_TRACE_FUZZER
  if set, the fuzzers will output trace (it is usually suppressed).

* GRPC_DNS_RESOLVER
  Declares which DNS resolver to use. The default is ares if gRPC is built with
  c-ares support. Otherwise, the value of this environment variable is ignored.
  Available DNS resolver include:
  - ares (default on most platforms except iOS, Android or Node)- a DNS
    resolver based around the c-ares library
  - native - a DNS resolver based around getaddrinfo(), creates a new thread to
    perform name resolution

  *NetBIOS and DNS*: If your network relies on NetBIOS name resolution or a mixture of
  DNS and NetBIOS name resolution (e.g. in some Windows networks) then you should use
  the '*native*' DNS resolver or make sure all NetBIOS names are
  also configured in DNS. The '*ares*' DNS resolver only supports DNS name resolution.

* GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS
  Default: 5000
  Declares the interval between two backup polls on client channels. These polls
  are run in the timer thread so that gRPC can process connection failures while
  there is no active polling thread. They help reconnect disconnected client
  channels (mostly due to idleness), so that the next RPC on this channel won't
  fail. Set to 0 to turn off the backup polls.

* grpc_cfstream
  set to 1 to turn on CFStream experiment. With this experiment gRPC uses CFStream API to make TCP
  connections. The option is only available on iOS platform and when macro GRPC_CFSTREAM is defined.
