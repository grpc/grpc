// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file intentionally does not have header guards, it's included
// inside a macro to generate enum values. The following line silences a
// presubmit and Tricium warning that would otherwise be triggered by this:
// no-include-guard-because-multiply-included
// NOLINT(build/header_guard)

// This file contains the list of network errors.

//
// Ranges:
//     0- 99 System related errors
//   100-199 Connection related errors
//   200-299 Certificate errors
//   300-399 HTTP errors
//   400-499 Cache errors
//   500-599 ?
//   600-699 FTP errors
//   700-799 Certificate manager errors
//   800-899 DNS resolver errors

// An asynchronous IO operation is not yet complete.  This usually does not
// indicate a fatal error.  Typically this error will be generated as a
// notification to wait for some external notification that the IO operation
// finally completed.
NET_ERROR(IO_PENDING, -1)

// A generic failure occurred.
NET_ERROR(FAILED, -2)

// An operation was aborted (due to user action).
NET_ERROR(ABORTED, -3)

// An argument to the function is incorrect.
NET_ERROR(INVALID_ARGUMENT, -4)

// The handle or file descriptor is invalid.
NET_ERROR(INVALID_HANDLE, -5)

// The file or directory cannot be found.
NET_ERROR(FILE_NOT_FOUND, -6)

// An operation timed out.
NET_ERROR(TIMED_OUT, -7)

// The file is too large.
NET_ERROR(FILE_TOO_BIG, -8)

// An unexpected error.  This may be caused by a programming mistake or an
// invalid assumption.
NET_ERROR(UNEXPECTED, -9)

// Permission to access a resource, other than the network, was denied.
NET_ERROR(ACCESS_DENIED, -10)

// The operation failed because of unimplemented functionality.
NET_ERROR(NOT_IMPLEMENTED, -11)

// There were not enough resources to complete the operation.
NET_ERROR(INSUFFICIENT_RESOURCES, -12)

// Memory allocation failed.
NET_ERROR(OUT_OF_MEMORY, -13)

// The file upload failed because the file's modification time was different
// from the expectation.
NET_ERROR(UPLOAD_FILE_CHANGED, -14)

// The socket is not connected.
NET_ERROR(SOCKET_NOT_CONNECTED, -15)

// The file already exists.
NET_ERROR(FILE_EXISTS, -16)

// The path or file name is too long.
NET_ERROR(FILE_PATH_TOO_LONG, -17)

// Not enough room left on the disk.
NET_ERROR(FILE_NO_SPACE, -18)

// The file has a virus.
NET_ERROR(FILE_VIRUS_INFECTED, -19)

// The client chose to block the request.
NET_ERROR(BLOCKED_BY_CLIENT, -20)

// The network changed.
NET_ERROR(NETWORK_CHANGED, -21)

// The request was blocked by the URL block list configured by the domain
// administrator.
NET_ERROR(BLOCKED_BY_ADMINISTRATOR, -22)

// The socket is already connected.
NET_ERROR(SOCKET_IS_CONNECTED, -23)

// The request was blocked because the forced reenrollment check is still
// pending. This error can only occur on ChromeOS.
// The error can be emitted by code in chrome/browser/policy/policy_helpers.cc.
NET_ERROR(BLOCKED_ENROLLMENT_CHECK_PENDING, -24)

// The upload failed because the upload stream needed to be re-read, due to a
// retry or a redirect, but the upload stream doesn't support that operation.
NET_ERROR(UPLOAD_STREAM_REWIND_NOT_SUPPORTED, -25)

// The request failed because the URLRequestContext is shutting down, or has
// been shut down.
NET_ERROR(CONTEXT_SHUT_DOWN, -26)

// The request failed because the response was delivered along with requirements
// which are not met ('X-Frame-Options' and 'Content-Security-Policy' ancestor
// checks and 'Cross-Origin-Resource-Policy', for instance).
NET_ERROR(BLOCKED_BY_RESPONSE, -27)

// Error -28 was removed (BLOCKED_BY_XSS_AUDITOR).

// The request was blocked by system policy disallowing some or all cleartext
// requests. Used for NetworkSecurityPolicy on Android.
NET_ERROR(CLEARTEXT_NOT_PERMITTED, -29)

// The request was blocked by a Content Security Policy
NET_ERROR(BLOCKED_BY_CSP, -30)

// The request was blocked because of no H/2 or QUIC session.
NET_ERROR(H2_OR_QUIC_REQUIRED, -31)

// The request was blocked because it is a private network request coming from
// an insecure context in a less private IP address space. This is used to
// enforce CORS-RFC1918: https://wicg.github.io/cors-rfc1918.
NET_ERROR(INSECURE_PRIVATE_NETWORK_REQUEST, -32)

// A connection was closed (corresponding to a TCP FIN).
NET_ERROR(CONNECTION_CLOSED, -100)

// A connection was reset (corresponding to a TCP RST).
NET_ERROR(CONNECTION_RESET, -101)

// A connection attempt was refused.
NET_ERROR(CONNECTION_REFUSED, -102)

// A connection timed out as a result of not receiving an ACK for data sent.
// This can include a FIN packet that did not get ACK'd.
NET_ERROR(CONNECTION_ABORTED, -103)

// A connection attempt failed.
NET_ERROR(CONNECTION_FAILED, -104)

// The host name could not be resolved.
NET_ERROR(NAME_NOT_RESOLVED, -105)

// The Internet connection has been lost.
NET_ERROR(INTERNET_DISCONNECTED, -106)

// An SSL protocol error occurred.
NET_ERROR(SSL_PROTOCOL_ERROR, -107)

// The IP address or port number is invalid (e.g., cannot connect to the IP
// address 0 or the port 0).
NET_ERROR(ADDRESS_INVALID, -108)

// The IP address is unreachable.  This usually means that there is no route to
// the specified host or network.
NET_ERROR(ADDRESS_UNREACHABLE, -109)

// The server requested a client certificate for SSL client authentication.
NET_ERROR(SSL_CLIENT_AUTH_CERT_NEEDED, -110)

// A tunnel connection through the proxy could not be established.
NET_ERROR(TUNNEL_CONNECTION_FAILED, -111)

// No SSL protocol versions are enabled.
NET_ERROR(NO_SSL_VERSIONS_ENABLED, -112)

// The client and server don't support a common SSL protocol version or
// cipher suite.
NET_ERROR(SSL_VERSION_OR_CIPHER_MISMATCH, -113)

// The server requested a renegotiation (rehandshake).
NET_ERROR(SSL_RENEGOTIATION_REQUESTED, -114)

// The proxy requested authentication (for tunnel establishment) with an
// unsupported method.
NET_ERROR(PROXY_AUTH_UNSUPPORTED, -115)

// During SSL renegotiation (rehandshake), the server sent a certificate with
// an error.
//
// Note: this error is not in the -2xx range so that it won't be handled as a
// certificate error.
NET_ERROR(CERT_ERROR_IN_SSL_RENEGOTIATION, -116)

// The SSL handshake failed because of a bad or missing client certificate.
NET_ERROR(BAD_SSL_CLIENT_AUTH_CERT, -117)

// A connection attempt timed out.
NET_ERROR(CONNECTION_TIMED_OUT, -118)

// There are too many pending DNS resolves, so a request in the queue was
// aborted.
NET_ERROR(HOST_RESOLVER_QUEUE_TOO_LARGE, -119)

// Failed establishing a connection to the SOCKS proxy server for a target host.
NET_ERROR(SOCKS_CONNECTION_FAILED, -120)

// The SOCKS proxy server failed establishing connection to the target host
// because that host is unreachable.
NET_ERROR(SOCKS_CONNECTION_HOST_UNREACHABLE, -121)

// The request to negotiate an alternate protocol failed.
NET_ERROR(ALPN_NEGOTIATION_FAILED, -122)

// The peer sent an SSL no_renegotiation alert message.
NET_ERROR(SSL_NO_RENEGOTIATION, -123)

// Winsock sometimes reports more data written than passed.  This is probably
// due to a broken LSP.
NET_ERROR(WINSOCK_UNEXPECTED_WRITTEN_BYTES, -124)

// An SSL peer sent us a fatal decompression_failure alert. This typically
// occurs when a peer selects DEFLATE compression in the mistaken belief that
// it supports it.
NET_ERROR(SSL_DECOMPRESSION_FAILURE_ALERT, -125)

// An SSL peer sent us a fatal bad_record_mac alert. This has been observed
// from servers with buggy DEFLATE support.
NET_ERROR(SSL_BAD_RECORD_MAC_ALERT, -126)

// The proxy requested authentication (for tunnel establishment).
NET_ERROR(PROXY_AUTH_REQUESTED, -127)

// Error -129 was removed (SSL_WEAK_SERVER_EPHEMERAL_DH_KEY).

// Could not create a connection to the proxy server. An error occurred
// either in resolving its name, or in connecting a socket to it.
// Note that this does NOT include failures during the actual "CONNECT" method
// of an HTTP proxy.
NET_ERROR(PROXY_CONNECTION_FAILED, -130)

// A mandatory proxy configuration could not be used. Currently this means
// that a mandatory PAC script could not be fetched, parsed or executed.
NET_ERROR(MANDATORY_PROXY_CONFIGURATION_FAILED, -131)

// -132 was formerly ERR_ESET_ANTI_VIRUS_SSL_INTERCEPTION

// We've hit the max socket limit for the socket pool while preconnecting.  We
// don't bother trying to preconnect more sockets.
NET_ERROR(PRECONNECT_MAX_SOCKET_LIMIT, -133)

// The permission to use the SSL client certificate's private key was denied.
NET_ERROR(SSL_CLIENT_AUTH_PRIVATE_KEY_ACCESS_DENIED, -134)

// The SSL client certificate has no private key.
NET_ERROR(SSL_CLIENT_AUTH_CERT_NO_PRIVATE_KEY, -135)

// The certificate presented by the HTTPS Proxy was invalid.
NET_ERROR(PROXY_CERTIFICATE_INVALID, -136)

// An error occurred when trying to do a name resolution (DNS).
NET_ERROR(NAME_RESOLUTION_FAILED, -137)

// Permission to access the network was denied. This is used to distinguish
// errors that were most likely caused by a firewall from other access denied
// errors. See also ERR_ACCESS_DENIED.
NET_ERROR(NETWORK_ACCESS_DENIED, -138)

// The request throttler module cancelled this request to avoid DDOS.
NET_ERROR(TEMPORARILY_THROTTLED, -139)

// A request to create an SSL tunnel connection through the HTTPS proxy
// received a 302 (temporary redirect) response.  The response body might
// include a description of why the request failed.
//
// TODO(https://crbug.com/928551): This is deprecated and should not be used by
// new code.
NET_ERROR(HTTPS_PROXY_TUNNEL_RESPONSE_REDIRECT, -140)

// We were unable to sign the CertificateVerify data of an SSL client auth
// handshake with the client certificate's private key.
//
// Possible causes for this include the user implicitly or explicitly
// denying access to the private key, the private key may not be valid for
// signing, the key may be relying on a cached handle which is no longer
// valid, or the CSP won't allow arbitrary data to be signed.
NET_ERROR(SSL_CLIENT_AUTH_SIGNATURE_FAILED, -141)

// The message was too large for the transport.  (for example a UDP message
// which exceeds size threshold).
NET_ERROR(MSG_TOO_BIG, -142)

// Error -143 was removed (SPDY_SESSION_ALREADY_EXISTS)

// Error -144 was removed (LIMIT_VIOLATION).

// Websocket protocol error. Indicates that we are terminating the connection
// due to a malformed frame or other protocol violation.
NET_ERROR(WS_PROTOCOL_ERROR, -145)

// Error -146 was removed (PROTOCOL_SWITCHED)

// Returned when attempting to bind an address that is already in use.
NET_ERROR(ADDRESS_IN_USE, -147)

// An operation failed because the SSL handshake has not completed.
NET_ERROR(SSL_HANDSHAKE_NOT_COMPLETED, -148)

// SSL peer's public key is invalid.
NET_ERROR(SSL_BAD_PEER_PUBLIC_KEY, -149)

// The certificate didn't match the built-in public key pins for the host name.
// The pins are set in net/http/transport_security_state.cc and require that
// one of a set of public keys exist on the path from the leaf to the root.
NET_ERROR(SSL_PINNED_KEY_NOT_IN_CERT_CHAIN, -150)

// Server request for client certificate did not contain any types we support.
NET_ERROR(CLIENT_AUTH_CERT_TYPE_UNSUPPORTED, -151)

// Error -152 was removed (ORIGIN_BOUND_CERT_GENERATION_TYPE_MISMATCH)

// An SSL peer sent us a fatal decrypt_error alert. This typically occurs when
// a peer could not correctly verify a signature (in CertificateVerify or
// ServerKeyExchange) or validate a Finished message.
NET_ERROR(SSL_DECRYPT_ERROR_ALERT, -153)

// There are too many pending WebSocketJob instances, so the new job was not
// pushed to the queue.
NET_ERROR(WS_THROTTLE_QUEUE_TOO_LARGE, -154)

// Error -155 was removed (TOO_MANY_SOCKET_STREAMS)

// The SSL server certificate changed in a renegotiation.
NET_ERROR(SSL_SERVER_CERT_CHANGED, -156)

// Error -157 was removed (SSL_INAPPROPRIATE_FALLBACK).

// Error -158 was removed (CT_NO_SCTS_VERIFIED_OK).

// The SSL server sent us a fatal unrecognized_name alert.
NET_ERROR(SSL_UNRECOGNIZED_NAME_ALERT, -159)

// Failed to set the socket's receive buffer size as requested.
NET_ERROR(SOCKET_SET_RECEIVE_BUFFER_SIZE_ERROR, -160)

// Failed to set the socket's send buffer size as requested.
NET_ERROR(SOCKET_SET_SEND_BUFFER_SIZE_ERROR, -161)

// Failed to set the socket's receive buffer size as requested, despite success
// return code from setsockopt.
NET_ERROR(SOCKET_RECEIVE_BUFFER_SIZE_UNCHANGEABLE, -162)

// Failed to set the socket's send buffer size as requested, despite success
// return code from setsockopt.
NET_ERROR(SOCKET_SEND_BUFFER_SIZE_UNCHANGEABLE, -163)

// Failed to import a client certificate from the platform store into the SSL
// library.
NET_ERROR(SSL_CLIENT_AUTH_CERT_BAD_FORMAT, -164)

// Error -165 was removed (SSL_FALLBACK_BEYOND_MINIMUM_VERSION).

// Resolving a hostname to an IP address list included the IPv4 address
// "127.0.53.53". This is a special IP address which ICANN has recommended to
// indicate there was a name collision, and alert admins to a potential
// problem.
NET_ERROR(ICANN_NAME_COLLISION, -166)

// The SSL server presented a certificate which could not be decoded. This is
// not a certificate error code as no X509Certificate object is available. This
// error is fatal.
NET_ERROR(SSL_SERVER_CERT_BAD_FORMAT, -167)

// Certificate Transparency: Received a signed tree head that failed to parse.
NET_ERROR(CT_STH_PARSING_FAILED, -168)

// Certificate Transparency: Received a signed tree head whose JSON parsing was
// OK but was missing some of the fields.
NET_ERROR(CT_STH_INCOMPLETE, -169)

// The attempt to reuse a connection to send proxy auth credentials failed
// before the AuthController was used to generate credentials. The caller should
// reuse the controller with a new connection. This error is only used
// internally by the network stack.
NET_ERROR(UNABLE_TO_REUSE_CONNECTION_FOR_PROXY_AUTH, -170)

// Certificate Transparency: Failed to parse the received consistency proof.
NET_ERROR(CT_CONSISTENCY_PROOF_PARSING_FAILED, -171)

// The SSL server required an unsupported cipher suite that has since been
// removed. This error will temporarily be signaled on a fallback for one or two
// releases immediately following a cipher suite's removal, after which the
// fallback will be removed.
NET_ERROR(SSL_OBSOLETE_CIPHER, -172)

// When a WebSocket handshake is done successfully and the connection has been
// upgraded, the URLRequest is cancelled with this error code.
NET_ERROR(WS_UPGRADE, -173)

// Socket ReadIfReady support is not implemented. This error should not be user
// visible, because the normal Read() method is used as a fallback.
NET_ERROR(READ_IF_READY_NOT_IMPLEMENTED, -174)

// Error -175 was removed (SSL_VERSION_INTERFERENCE).

// No socket buffer space is available.
NET_ERROR(NO_BUFFER_SPACE, -176)

// There were no common signature algorithms between our client certificate
// private key and the server's preferences.
NET_ERROR(SSL_CLIENT_AUTH_NO_COMMON_ALGORITHMS, -177)

// TLS 1.3 early data was rejected by the server. This will be received before
// any data is returned from the socket. The request should be retried with
// early data disabled.
NET_ERROR(EARLY_DATA_REJECTED, -178)

// TLS 1.3 early data was offered, but the server responded with TLS 1.2 or
// earlier. This is an internal error code to account for a
// backwards-compatibility issue with early data and TLS 1.2. It will be
// received before any data is returned from the socket. The request should be
// retried with early data disabled.
//
// See https://tools.ietf.org/html/rfc8446#appendix-D.3 for details.
NET_ERROR(WRONG_VERSION_ON_EARLY_DATA, -179)

// TLS 1.3 was enabled, but a lower version was negotiated and the server
// returned a value indicating it supported TLS 1.3. This is part of a security
// check in TLS 1.3, but it may also indicate the user is behind a buggy
// TLS-terminating proxy which implemented TLS 1.2 incorrectly. (See
// https://crbug.com/boringssl/226.)
NET_ERROR(TLS13_DOWNGRADE_DETECTED, -180)

// The server's certificate has a keyUsage extension incompatible with the
// negotiated TLS key exchange method.
NET_ERROR(SSL_KEY_USAGE_INCOMPATIBLE, -181)

// Certificate error codes
//
// The values of certificate error codes must be consecutive.

// The server responded with a certificate whose common name did not match
// the host name.  This could mean:
//
// 1. An attacker has redirected our traffic to their server and is
//    presenting a certificate for which they know the private key.
//
// 2. The server is misconfigured and responding with the wrong cert.
//
// 3. The user is on a wireless network and is being redirected to the
//    network's login page.
//
// 4. The OS has used a DNS search suffix and the server doesn't have
//    a certificate for the abbreviated name in the address bar.
//
NET_ERROR(CERT_COMMON_NAME_INVALID, -200)

// The server responded with a certificate that, by our clock, appears to
// either not yet be valid or to have expired.  This could mean:
//
// 1. An attacker is presenting an old certificate for which they have
//    managed to obtain the private key.
//
// 2. The server is misconfigured and is not presenting a valid cert.
//
// 3. Our clock is wrong.
//
NET_ERROR(CERT_DATE_INVALID, -201)

// The server responded with a certificate that is signed by an authority
// we don't trust.  The could mean:
//
// 1. An attacker has substituted the real certificate for a cert that
//    contains their public key and is signed by their cousin.
//
// 2. The server operator has a legitimate certificate from a CA we don't
//    know about, but should trust.
//
// 3. The server is presenting a self-signed certificate, providing no
//    defense against active attackers (but foiling passive attackers).
//
NET_ERROR(CERT_AUTHORITY_INVALID, -202)

// The server responded with a certificate that contains errors.
// This error is not recoverable.
//
// MSDN describes this error as follows:
//   "The SSL certificate contains errors."
// NOTE: It's unclear how this differs from ERR_CERT_INVALID. For consistency,
// use that code instead of this one from now on.
//
NET_ERROR(CERT_CONTAINS_ERRORS, -203)

// The certificate has no mechanism for determining if it is revoked.  In
// effect, this certificate cannot be revoked.
NET_ERROR(CERT_NO_REVOCATION_MECHANISM, -204)

// Revocation information for the security certificate for this site is not
// available.  This could mean:
//
// 1. An attacker has compromised the private key in the certificate and is
//    blocking our attempt to find out that the cert was revoked.
//
// 2. The certificate is unrevoked, but the revocation server is busy or
//    unavailable.
//
NET_ERROR(CERT_UNABLE_TO_CHECK_REVOCATION, -205)

// The server responded with a certificate has been revoked.
// We have the capability to ignore this error, but it is probably not the
// thing to do.
NET_ERROR(CERT_REVOKED, -206)

// The server responded with a certificate that is invalid.
// This error is not recoverable.
//
// MSDN describes this error as follows:
//   "The SSL certificate is invalid."
//
NET_ERROR(CERT_INVALID, -207)

// The server responded with a certificate that is signed using a weak
// signature algorithm.
NET_ERROR(CERT_WEAK_SIGNATURE_ALGORITHM, -208)

// -209 is availible: was CERT_NOT_IN_DNS.

// The host name specified in the certificate is not unique.
NET_ERROR(CERT_NON_UNIQUE_NAME, -210)

// The server responded with a certificate that contains a weak key (e.g.
// a too-small RSA key).
NET_ERROR(CERT_WEAK_KEY, -211)

// The certificate claimed DNS names that are in violation of name constraints.
NET_ERROR(CERT_NAME_CONSTRAINT_VIOLATION, -212)

// The certificate's validity period is too long.
NET_ERROR(CERT_VALIDITY_TOO_LONG, -213)

// Certificate Transparency was required for this connection, but the server
// did not provide CT information that complied with the policy.
NET_ERROR(CERTIFICATE_TRANSPARENCY_REQUIRED, -214)

// The certificate chained to a legacy Symantec root that is no longer trusted.
// https://g.co/chrome/symantecpkicerts
NET_ERROR(CERT_SYMANTEC_LEGACY, -215)

// -216 was QUIC_CERT_ROOT_NOT_KNOWN which has been renumbered to not be in the
// certificate error range.

// The certificate is known to be used for interception by an entity other
// the device owner.
NET_ERROR(CERT_KNOWN_INTERCEPTION_BLOCKED, -217)

// The connection uses an obsolete version of SSL/TLS.
NET_ERROR(SSL_OBSOLETE_VERSION, -218)

// Add new certificate error codes here.
//
// Update the value of CERT_END whenever you add a new certificate error
// code.

// The value immediately past the last certificate error code.
NET_ERROR(CERT_END, -219)

// The URL is invalid.
NET_ERROR(INVALID_URL, -300)

// The scheme of the URL is disallowed.
NET_ERROR(DISALLOWED_URL_SCHEME, -301)

// The scheme of the URL is unknown.
NET_ERROR(UNKNOWN_URL_SCHEME, -302)

// Attempting to load an URL resulted in a redirect to an invalid URL.
NET_ERROR(INVALID_REDIRECT, -303)

// Attempting to load an URL resulted in too many redirects.
NET_ERROR(TOO_MANY_REDIRECTS, -310)

// Attempting to load an URL resulted in an unsafe redirect (e.g., a redirect
// to file:// is considered unsafe).
NET_ERROR(UNSAFE_REDIRECT, -311)

// Attempting to load an URL with an unsafe port number.  These are port
// numbers that correspond to services, which are not robust to spurious input
// that may be constructed as a result of an allowed web construct (e.g., HTTP
// looks a lot like SMTP, so form submission to port 25 is denied).
NET_ERROR(UNSAFE_PORT, -312)

// The server's response was invalid.
NET_ERROR(INVALID_RESPONSE, -320)

// Error in chunked transfer encoding.
NET_ERROR(INVALID_CHUNKED_ENCODING, -321)

// The server did not support the request method.
NET_ERROR(METHOD_NOT_SUPPORTED, -322)

// The response was 407 (Proxy Authentication Required), yet we did not send
// the request to a proxy.
NET_ERROR(UNEXPECTED_PROXY_AUTH, -323)

// The server closed the connection without sending any data.
NET_ERROR(EMPTY_RESPONSE, -324)

// The headers section of the response is too large.
NET_ERROR(RESPONSE_HEADERS_TOO_BIG, -325)

// Error -326 was removed (PAC_STATUS_NOT_OK)

// The evaluation of the PAC script failed.
NET_ERROR(PAC_SCRIPT_FAILED, -327)

// The response was 416 (Requested range not satisfiable) and the server cannot
// satisfy the range requested.
NET_ERROR(REQUEST_RANGE_NOT_SATISFIABLE, -328)

// The identity used for authentication is invalid.
NET_ERROR(MALFORMED_IDENTITY, -329)

// Content decoding of the response body failed.
NET_ERROR(CONTENT_DECODING_FAILED, -330)

// An operation could not be completed because all network IO
// is suspended.
NET_ERROR(NETWORK_IO_SUSPENDED, -331)

// FLIP data received without receiving a SYN_REPLY on the stream.
NET_ERROR(SYN_REPLY_NOT_RECEIVED, -332)

// Converting the response to target encoding failed.
NET_ERROR(ENCODING_CONVERSION_FAILED, -333)

// The server sent an FTP directory listing in a format we do not understand.
NET_ERROR(UNRECOGNIZED_FTP_DIRECTORY_LISTING_FORMAT, -334)

// Obsolete.  Was only logged in NetLog when an HTTP/2 pushed stream expired.
// NET_ERROR(INVALID_SPDY_STREAM, -335)

// There are no supported proxies in the provided list.
NET_ERROR(NO_SUPPORTED_PROXIES, -336)

// There is an HTTP/2 protocol error.
NET_ERROR(HTTP2_PROTOCOL_ERROR, -337)

// Credentials could not be established during HTTP Authentication.
NET_ERROR(INVALID_AUTH_CREDENTIALS, -338)

// An HTTP Authentication scheme was tried which is not supported on this
// machine.
NET_ERROR(UNSUPPORTED_AUTH_SCHEME, -339)

// Detecting the encoding of the response failed.
NET_ERROR(ENCODING_DETECTION_FAILED, -340)

// (GSSAPI) No Kerberos credentials were available during HTTP Authentication.
NET_ERROR(MISSING_AUTH_CREDENTIALS, -341)

// An unexpected, but documented, SSPI or GSSAPI status code was returned.
NET_ERROR(UNEXPECTED_SECURITY_LIBRARY_STATUS, -342)

// The environment was not set up correctly for authentication (for
// example, no KDC could be found or the principal is unknown.
NET_ERROR(MISCONFIGURED_AUTH_ENVIRONMENT, -343)

// An undocumented SSPI or GSSAPI status code was returned.
NET_ERROR(UNDOCUMENTED_SECURITY_LIBRARY_STATUS, -344)

// The HTTP response was too big to drain.
NET_ERROR(RESPONSE_BODY_TOO_BIG_TO_DRAIN, -345)

// The HTTP response contained multiple distinct Content-Length headers.
NET_ERROR(RESPONSE_HEADERS_MULTIPLE_CONTENT_LENGTH, -346)

// HTTP/2 headers have been received, but not all of them - status or version
// headers are missing, so we're expecting additional frames to complete them.
NET_ERROR(INCOMPLETE_HTTP2_HEADERS, -347)

// No PAC URL configuration could be retrieved from DHCP. This can indicate
// either a failure to retrieve the DHCP configuration, or that there was no
// PAC URL configured in DHCP.
NET_ERROR(PAC_NOT_IN_DHCP, -348)

// The HTTP response contained multiple Content-Disposition headers.
NET_ERROR(RESPONSE_HEADERS_MULTIPLE_CONTENT_DISPOSITION, -349)

// The HTTP response contained multiple Location headers.
NET_ERROR(RESPONSE_HEADERS_MULTIPLE_LOCATION, -350)

// HTTP/2 server refused the request without processing, and sent either a
// GOAWAY frame with error code NO_ERROR and Last-Stream-ID lower than the
// stream id corresponding to the request indicating that this request has not
// been processed yet, or a RST_STREAM frame with error code REFUSED_STREAM.
// Client MAY retry (on a different connection).  See RFC7540 Section 8.1.4.
NET_ERROR(HTTP2_SERVER_REFUSED_STREAM, -351)

// HTTP/2 server didn't respond to the PING message.
NET_ERROR(HTTP2_PING_FAILED, -352)

// Obsolete.  Kept here to avoid reuse, as the old error can still appear on
// histograms.
// NET_ERROR(PIPELINE_EVICTION, -353)

// The HTTP response body transferred fewer bytes than were advertised by the
// Content-Length header when the connection is closed.
NET_ERROR(CONTENT_LENGTH_MISMATCH, -354)

// The HTTP response body is transferred with Chunked-Encoding, but the
// terminating zero-length chunk was never sent when the connection is closed.
NET_ERROR(INCOMPLETE_CHUNKED_ENCODING, -355)

// There is a QUIC protocol error.
NET_ERROR(QUIC_PROTOCOL_ERROR, -356)

// The HTTP headers were truncated by an EOF.
NET_ERROR(RESPONSE_HEADERS_TRUNCATED, -357)

// The QUIC crytpo handshake failed.  This means that the server was unable
// to read any requests sent, so they may be resent.
NET_ERROR(QUIC_HANDSHAKE_FAILED, -358)

// Obsolete.  Kept here to avoid reuse, as the old error can still appear on
// histograms.
// NET_ERROR(REQUEST_FOR_SECURE_RESOURCE_OVER_INSECURE_QUIC, -359)

// Transport security is inadequate for the HTTP/2 version.
NET_ERROR(HTTP2_INADEQUATE_TRANSPORT_SECURITY, -360)

// The peer violated HTTP/2 flow control.
NET_ERROR(HTTP2_FLOW_CONTROL_ERROR, -361)

// The peer sent an improperly sized HTTP/2 frame.
NET_ERROR(HTTP2_FRAME_SIZE_ERROR, -362)

// Decoding or encoding of compressed HTTP/2 headers failed.
NET_ERROR(HTTP2_COMPRESSION_ERROR, -363)

// Proxy Auth Requested without a valid Client Socket Handle.
NET_ERROR(PROXY_AUTH_REQUESTED_WITH_NO_CONNECTION, -364)

// HTTP_1_1_REQUIRED error code received on HTTP/2 session.
NET_ERROR(HTTP_1_1_REQUIRED, -365)

// HTTP_1_1_REQUIRED error code received on HTTP/2 session to proxy.
NET_ERROR(PROXY_HTTP_1_1_REQUIRED, -366)

// The PAC script terminated fatally and must be reloaded.
NET_ERROR(PAC_SCRIPT_TERMINATED, -367)

// Obsolete. Kept here to avoid reuse.
// Request is throttled because of a Backoff header.
// See: crbug.com/486891.
// NET_ERROR(TEMPORARY_BACKOFF, -369)

// The server was expected to return an HTTP/1.x response, but did not. Rather
// than treat it as HTTP/0.9, this error is returned.
NET_ERROR(INVALID_HTTP_RESPONSE, -370)

// Initializing content decoding failed.
NET_ERROR(CONTENT_DECODING_INIT_FAILED, -371)

// Received HTTP/2 RST_STREAM frame with NO_ERROR error code.  This error should
// be handled internally by HTTP/2 code, and should not make it above the
// SpdyStream layer.
NET_ERROR(HTTP2_RST_STREAM_NO_ERROR_RECEIVED, -372)

// The pushed stream claimed by the request is no longer available.
NET_ERROR(HTTP2_PUSHED_STREAM_NOT_AVAILABLE, -373)

// A pushed stream was claimed and later reset by the server. When this happens,
// the request should be retried.
NET_ERROR(HTTP2_CLAIMED_PUSHED_STREAM_RESET_BY_SERVER, -374)

// An HTTP transaction was retried too many times due for authentication or
// invalid certificates. This may be due to a bug in the net stack that would
// otherwise infinite loop, or if the server or proxy continually requests fresh
// credentials or presents a fresh invalid certificate.
NET_ERROR(TOO_MANY_RETRIES, -375)

// Received an HTTP/2 frame on a closed stream.
NET_ERROR(HTTP2_STREAM_CLOSED, -376)

// Client is refusing an HTTP/2 stream.
NET_ERROR(HTTP2_CLIENT_REFUSED_STREAM, -377)

// A pushed HTTP/2 stream was claimed by a request based on matching URL and
// request headers, but the pushed response headers do not match the request.
NET_ERROR(HTTP2_PUSHED_RESPONSE_DOES_NOT_MATCH, -378)

// The server returned a non-2xx HTTP response code.
//
// Not that this error is only used by certain APIs that interpret the HTTP
// response itself. URLRequest for instance just passes most non-2xx
// response back as success.
NET_ERROR(HTTP_RESPONSE_CODE_FAILURE, -379)

// The certificate presented on a QUIC connection does not chain to a known root
// and the origin connected to is not on a list of domains where unknown roots
// are allowed.
NET_ERROR(QUIC_CERT_ROOT_NOT_KNOWN, -380)

// The cache does not have the requested entry.
NET_ERROR(CACHE_MISS, -400)

// Unable to read from the disk cache.
NET_ERROR(CACHE_READ_FAILURE, -401)

// Unable to write to the disk cache.
NET_ERROR(CACHE_WRITE_FAILURE, -402)

// The operation is not supported for this entry.
NET_ERROR(CACHE_OPERATION_NOT_SUPPORTED, -403)

// The disk cache is unable to open this entry.
NET_ERROR(CACHE_OPEN_FAILURE, -404)

// The disk cache is unable to create this entry.
NET_ERROR(CACHE_CREATE_FAILURE, -405)

// Multiple transactions are racing to create disk cache entries. This is an
// internal error returned from the HttpCache to the HttpCacheTransaction that
// tells the transaction to restart the entry-creation logic because the state
// of the cache has changed.
NET_ERROR(CACHE_RACE, -406)

// The cache was unable to read a checksum record on an entry. This can be
// returned from attempts to read from the cache. It is an internal error,
// returned by the SimpleCache backend, but not by any URLRequest methods
// or members.
NET_ERROR(CACHE_CHECKSUM_READ_FAILURE, -407)

// The cache found an entry with an invalid checksum. This can be returned from
// attempts to read from the cache. It is an internal error, returned by the
// SimpleCache backend, but not by any URLRequest methods or members.
NET_ERROR(CACHE_CHECKSUM_MISMATCH, -408)

// Internal error code for the HTTP cache. The cache lock timeout has fired.
NET_ERROR(CACHE_LOCK_TIMEOUT, -409)

// Received a challenge after the transaction has read some data, and the
// credentials aren't available.  There isn't a way to get them at that point.
NET_ERROR(CACHE_AUTH_FAILURE_AFTER_READ, -410)

// Internal not-quite error code for the HTTP cache. In-memory hints suggest
// that the cache entry would not have been useable with the transaction's
// current configuration (e.g. load flags, mode, etc.)
NET_ERROR(CACHE_ENTRY_NOT_SUITABLE, -411)

// The disk cache is unable to doom this entry.
NET_ERROR(CACHE_DOOM_FAILURE, -412)

// The disk cache is unable to open or create this entry.
NET_ERROR(CACHE_OPEN_OR_CREATE_FAILURE, -413)

// The server's response was insecure (e.g. there was a cert error).
NET_ERROR(INSECURE_RESPONSE, -501)

// An attempt to import a client certificate failed, as the user's key
// database lacked a corresponding private key.
NET_ERROR(NO_PRIVATE_KEY_FOR_CERT, -502)

// An error adding a certificate to the OS certificate database.
NET_ERROR(ADD_USER_CERT_FAILED, -503)

// An error occurred while handling a signed exchange.
NET_ERROR(INVALID_SIGNED_EXCHANGE, -504)

// An error occurred while handling a Web Bundle source.
NET_ERROR(INVALID_WEB_BUNDLE, -505)

// A Trust Tokens protocol operation-executing request failed for one of a
// number of reasons (precondition failure, internal error, bad response).
NET_ERROR(TRUST_TOKEN_OPERATION_FAILED, -506)

// When handling a Trust Tokens protocol operation-executing request, the system
// found that the request's desired Trust Tokens results were already present in
// a local cache; as a result, the main request was cancelled.
NET_ERROR(TRUST_TOKEN_OPERATION_CACHE_HIT, -507)

// *** Code -600 is reserved (was FTP_PASV_COMMAND_FAILED). ***

// A generic error for failed FTP control connection command.
// If possible, please use or add a more specific error code.
NET_ERROR(FTP_FAILED, -601)

// The server cannot fulfill the request at this point. This is a temporary
// error.
// FTP response code 421.
NET_ERROR(FTP_SERVICE_UNAVAILABLE, -602)

// The server has aborted the transfer.
// FTP response code 426.
NET_ERROR(FTP_TRANSFER_ABORTED, -603)

// The file is busy, or some other temporary error condition on opening
// the file.
// FTP response code 450.
NET_ERROR(FTP_FILE_BUSY, -604)

// Server rejected our command because of syntax errors.
// FTP response codes 500, 501.
NET_ERROR(FTP_SYNTAX_ERROR, -605)

// Server does not support the command we issued.
// FTP response codes 502, 504.
NET_ERROR(FTP_COMMAND_NOT_SUPPORTED, -606)

// Server rejected our command because we didn't issue the commands in right
// order.
// FTP response code 503.
NET_ERROR(FTP_BAD_COMMAND_SEQUENCE, -607)

// PKCS #12 import failed due to incorrect password.
NET_ERROR(PKCS12_IMPORT_BAD_PASSWORD, -701)

// PKCS #12 import failed due to other error.
NET_ERROR(PKCS12_IMPORT_FAILED, -702)

// CA import failed - not a CA cert.
NET_ERROR(IMPORT_CA_CERT_NOT_CA, -703)

// Import failed - certificate already exists in database.
// Note it's a little weird this is an error but reimporting a PKCS12 is ok
// (no-op).  That's how Mozilla does it, though.
NET_ERROR(IMPORT_CERT_ALREADY_EXISTS, -704)

// CA import failed due to some other error.
NET_ERROR(IMPORT_CA_CERT_FAILED, -705)

// Server certificate import failed due to some internal error.
NET_ERROR(IMPORT_SERVER_CERT_FAILED, -706)

// PKCS #12 import failed due to invalid MAC.
NET_ERROR(PKCS12_IMPORT_INVALID_MAC, -707)

// PKCS #12 import failed due to invalid/corrupt file.
NET_ERROR(PKCS12_IMPORT_INVALID_FILE, -708)

// PKCS #12 import failed due to unsupported features.
NET_ERROR(PKCS12_IMPORT_UNSUPPORTED, -709)

// Key generation failed.
NET_ERROR(KEY_GENERATION_FAILED, -710)

// Error -711 was removed (ORIGIN_BOUND_CERT_GENERATION_FAILED)

// Failure to export private key.
NET_ERROR(PRIVATE_KEY_EXPORT_FAILED, -712)

// Self-signed certificate generation failed.
NET_ERROR(SELF_SIGNED_CERT_GENERATION_FAILED, -713)

// The certificate database changed in some way.
NET_ERROR(CERT_DATABASE_CHANGED, -714)

// Error -715 was removed (CHANNEL_ID_IMPORT_FAILED)

// DNS error codes.

// DNS resolver received a malformed response.
NET_ERROR(DNS_MALFORMED_RESPONSE, -800)

// DNS server requires TCP
NET_ERROR(DNS_SERVER_REQUIRES_TCP, -801)

// DNS server failed.  This error is returned for all of the following
// error conditions:
// 1 - Format error - The name server was unable to interpret the query.
// 2 - Server failure - The name server was unable to process this query
//     due to a problem with the name server.
// 4 - Not Implemented - The name server does not support the requested
//     kind of query.
// 5 - Refused - The name server refuses to perform the specified
//     operation for policy reasons.
NET_ERROR(DNS_SERVER_FAILED, -802)

// DNS transaction timed out.
NET_ERROR(DNS_TIMED_OUT, -803)

// The entry was not found in cache or other local sources, for lookups where
// only local sources were queried.
// TODO(ericorth): Consider renaming to DNS_LOCAL_MISS or something like that as
// the cache is not necessarily queried either.
NET_ERROR(DNS_CACHE_MISS, -804)

// Suffix search list rules prevent resolution of the given host name.
NET_ERROR(DNS_SEARCH_EMPTY, -805)

// Failed to sort addresses according to RFC3484.
NET_ERROR(DNS_SORT_ERROR, -806)

// Error -807 was removed (DNS_HTTP_FAILED)

// Failed to resolve the hostname of a DNS-over-HTTPS server.
NET_ERROR(DNS_SECURE_RESOLVER_HOSTNAME_RESOLUTION_FAILED, -808)