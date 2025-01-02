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
  A comma-separated list of tracer names or glob patterns that provide
  additional insight into how gRPC C core is processing requests via debug logs.
  Available tracers and their usage can be found in
  [gRPC Trace Flags](trace_flags.md)

* GRPC_VERBOSITY (DEPRECATED)

<!-- BEGIN_OPEN_SOURCE_DOCUMENTATION -->
  `GRPC_VERBOSITY` is used to set the minimum level of log messages printed. Supported values are `DEBUG`, `INFO`, `ERROR` and `NONE`.

  We only support this flag for legacy reasons. If this environment variable is set, then gRPC will set absl MinLogValue and absl SetVLogLevel. This will alter the log settings of the entire application, not just gRPC code. For that reason, it is not recommended. Our recommendation is to avoid using this flag and [set log verbosity using absl](https://abseil.io/docs/cpp/guides/logging).

  gRPC logging verbosity - one of:
  - DEBUG - log INFO, WARNING, ERROR and FATAL messages. Also sets absl VLOG(2) logs enabled. This is not recommended for production systems. This will be expensive for staging environments too, so it can be used when you want to debug a specific issue. 
  - INFO - log INFO, WARNING, ERROR and FATAL messages. This is not recommended for production systems. This may be slightly expensive for staging environments too. We recommend that you use your discretion for staging environments.
  - ERROR - log ERROR and FATAL messages. This is recommended for production systems.
  - NONE - won't log any.
  GRPC_VERBOSITY will set verbosity of absl logging. 
  - If the external application sets some other verbosity, then whatever is set later will be honoured. 
  - If nothing is set as GRPC_VERBOSITY, then the setting of the external application will be honoured.
  - If nothing is set by the external application also, the default set by absl will be honoured.
<!-- END_OPEN_SOURCE_DOCUMENTATION -->

* GRPC_STACKTRACE_MINLOGLEVEL (DEPRECATED)
  This will not work anymore.

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
