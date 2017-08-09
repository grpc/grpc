[This code's documentation lives on the grpc.io site.](https://grpc.io/docs/quickstart/python.html)

running the "with_server_creds_reload" example:

server:

```
helloworld $ GRPC_VERBOSITY=DEBUG python greeter_server_with_server_creds_reload.py 
I0809 08:12:15.900122073    8213 ev_epollsig_linux.c:71]     epoll engine will be using signal: 40
D0809 08:12:15.900140368    8213 ev_posix.c:111]             Using polling engine: epollsig
D0809 08:12:15.900159687    8213 dns_resolver.c:301]         Using native dns resolver
D0809 08:12:22.088572521    8218 security_connector.c:539]   no change in server credentials
D0809 08:12:24.098690376    8218 security_connector.c:539]   no change in server credentials
D0809 08:12:26.117429849    8218 security_connector.c:543]   NEW server credentials!
D0809 08:12:26.119056668    8218 security_connector.c:572]   successfully created new handshaker factory
D0809 08:12:26.131396835    8218 security_handshaker.c:111]  Security handshake failed: {"created":"@1502291546.131373712","description":"Handshake read failed","file":"src/core/lib/security/transport/security_handshaker.c","file_line":289,"referenced_errors":[{"created":"@1502291546.131372463","description":"Socket closed","fd":6,"file":"src/core/lib/iomgr/tcp_posix.c","file_line":285,"target_address":"ipv4:127.0.0.1:36360"}]}
D0809 08:12:26.131437631    8218 chttp2_server.c:68]         Handshaking failed: {"created":"@1502291546.131373712","description":"Handshake read failed","file":"src/core/lib/security/transport/security_handshaker.c","file_line":289,"referenced_errors":[{"created":"@1502291546.131372463","description":"Socket closed","fd":6,"file":"src/core/lib/iomgr/tcp_posix.c","file_line":285,"target_address":"ipv4:127.0.0.1:36360"}]}
D0809 08:12:28.136186869    8218 security_connector.c:539]   no change in server credentials
D0809 08:12:30.154647816    8218 security_connector.c:539]   no change in server credentials
```

client:

```
helloworld $ GRPC_VERBOSITY=DEBUG python greeter_client_with_server_creds_reload.py 
I0809 08:12:22.084258639    8221 ev_epollsig_linux.c:71]     epoll engine will be using signal: 40
D0809 08:12:22.084275539    8221 ev_posix.c:111]             Using polling engine: epollsig
D0809 08:12:22.084315217    8221 dns_resolver.c:301]         Using native dns resolver
--------------------------------------------------
using ca cert1.pem
Greeter client received: Hello, you!
--------------------------------------------------
using ca cert1.pem
Greeter client received: Hello, you!
--------------------------------------------------
using ca cert1.pem
E0809 08:12:26.131323133    8237 ssl_transport_security.c:921] Handshake failed with fatal error SSL_ERROR_SSL: error:1000007d:SSL routines:OPENSSL_internal:CERTIFICATE_VERIFY_FAILED.
D0809 08:12:26.131347069    8237 security_handshaker.c:111]  Security handshake failed: {"created":"@1502291546.131338801","description":"Handshake failed","file":"src/core/lib/security/transport/security_handshaker.c","file_line":213,"tsi_code":10,"tsi_error":"TSI_PROTOCOL_FAILURE"}
I0809 08:12:26.131400584    8237 subchannel.c:683]           Connect failed: {"created":"@1502291546.131338801","description":"Handshake failed","file":"src/core/lib/security/transport/security_handshaker.c","file_line":213,"tsi_code":10,"tsi_error":"TSI_PROTOCOL_FAILURE"}
I0809 08:12:26.131413923    8237 subchannel.c:479]           Retry in 19.985686157 seconds
handshake fails? switching ca cert
--------------------------------------------------
using ca cert2.pem
Greeter client received: Hello, you!
--------------------------------------------------
using ca cert2.pem
Greeter client received: Hello, you!
helloworld $
```
