#gRPC Authentication support

gRPC is designed to plug-in a number of authentication mechanisms. We provide an overview 
of the various auth mechanisms supported, discuss the API and demonstrate usage through 
code examples, and conclude with a discussion of extensibility.

###SSL/TLS
gRPC has SSL/TLS integration and promotes the use of SSL/TLS to authenticate the server,
and encrypt all the data exchanged between the client and the server. Optional 
mechanisms are available for clients to provide certificates to accomplish mutual 
authentication.

###OAuth 2.0
gRPC provides a generic mechanism (described below) to attach metadata to requests 
and responses. This mechanism can be used to attach OAuth 2.0 Access Tokens to 
RPCs being made at a client. Additional support for acquiring Access Tokens while 
accessing Google APIs through gRPC is provided for certain auth flows, demonstrated 
through code examples below.

###API
To reduce complexity and minimize API clutter, gRPC works with a unified concept of 
a Credentials object. Users construct gRPC credentials using corresponding bootstrap 
credentials (e.g., SSL client certs or Service Account Keys), and use the 
credentials while creating a gRPC channel to any server. Depending on the type of 
credential supplied, the channel uses the credentials during the initial SSL/TLS 
handshake with the server, or uses  the credential to generate and attach Access
Tokens to each request being made on the channel.

###Code Examples

####SSL/TLS for server authentication and encryption
This is the simplest authentication scenario, where a client just wants to
authenticate the server and encrypt all data.

```
SslCredentialsOptions ssl_opts;  // Options to override SSL params, empty by default 
// Create the credentials object by providing service account key in constructor
std::unique_ptr<Credentials> creds = CredentialsFactory::SslCredentials(ssl_opts);
// Create a channel using the credentials created in the previous step
std::shared_ptr<ChannelInterface> channel = CreateChannel(server_name, creds, channel_args);
// Create a stub on the channel
std::unique_ptr<Greeter::Stub> stub(Greeter::NewStub(channel));
// Make actual RPC calls on the stub. 
grpc::Status s = stub->sayHello(&context, *request, response);
```

For advanced use cases such as modifying the root CA or using client certs, 
the corresponding options can be set in the SslCredentialsOptions parameter 
passed to the factory method.


###Authenticating with Google

gRPC applications can use a simple API to create a credential that works in various deployment scenarios.

```
std::unique_ptr<Credentials> creds = CredentialsFactory::DefaultGoogleCredentials();
// Create a channel, stub and make RPC calls (same as in the previous example)
std::shared_ptr<ChannelInterface> channel = CreateChannel(server_name, creds, channel_args);
std::unique_ptr<Greeter::Stub> stub(Greeter::NewStub(channel));
grpc::Status s = stub->sayHello(&context, *request, response);
```

This credential works for applications using Service Accounts as well as for 
applications running in Google Compute Engine (GCE). In the former case, the
service account’s private keys are expected in file located at [TODO: well
known file fath for service account keys] or in the file named in the environment
variable [TODO: add the env var name here]. The keys are used at run-time to
generate bearer tokens that are attached to each outgoing RPC on the
corresponding channel.

For applications running in GCE, a default service account and corresponding
OAuth scopes can be configured during VM setup. At run-time, this credential
handles communication with the authentication systems to obtain OAuth2 access
tokens and attaches them to each outgoing RPC on the corresponding channel.
Extending gRPC to support other authentication mechanisms
The gRPC protocol is designed with a general mechanism for sending metadata
associated with RPC. Clients can send metadata at the beginning of an RPC and
servers can send back metadata at the beginning and end of the RPC. This 
provides a natural mechanism to support OAuth2 and other authentication 
mechanisms that need attach bearer tokens to individual request. 

In the simplest case, there is a single line of code required on the client
to add a specific token as metadata to an RPC and a corresponding access on 
the server to retrieve this piece of metadata. The generation of the token 
on the client side and its verification at the server can be done separately.

A deeper integration can be achieved by plugging in a gRPC credentials implementation for any custom authentication mechanism that needs to attach per-request tokens. gRPC internals also allow switching out SSL/TLS with other encryption mechanisms. 
