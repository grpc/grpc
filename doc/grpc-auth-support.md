#gRPC Authentication support

gRPC is designed to plug-in a number of authentication mechanisms. This document
provides a quick overview of the various auth mechanisms supported, discusses
the API with some examples, and concludes with a discussion of extensibility.
More documentation and examples are coming soon!

## Supported auth mechanisms

###SSL/TLS
gRPC has SSL/TLS integration and promotes the use of SSL/TLS to authenticate the
server, and encrypt all the data exchanged between the client and the server.
Optional mechanisms are available for clients to provide certificates to
accomplish mutual authentication.

###OAuth 2.0
gRPC provides a generic mechanism (described below) to attach metadata to
requests and responses. This mechanism can be used to attach OAuth 2.0 Access
Tokens to RPCs being made at a client. Additional support for acquiring Access
Tokens while accessing Google APIs through gRPC is provided for certain auth
flows, demonstrated through code examples below.

## API
To reduce complexity and minimize API clutter, gRPC works with a unified concept
of a Credentials object. Users construct gRPC credentials using corresponding
bootstrap credentials (e.g., SSL client certs or Service Account Keys), and use
the credentials while creating a gRPC channel to any server. Depending on the
type of credential supplied, the channel uses the credentials during the initial
SSL/TLS handshake with the server, or uses  the credential to generate and
attach Access Tokens to each request being made on the channel.

###SSL/TLS for server authentication and encryption
This is the simplest authentication scenario, where a client just wants to
authenticate the server and encrypt all data.

```cpp
SslCredentialsOptions ssl_opts;  // Options to override SSL params, empty by default
// Create the credentials object by providing service account key in constructor
std::shared_ptr<ChannelCredentials> creds = SslCredentials(ssl_opts);
// Create a channel using the credentials created in the previous step
std::shared_ptr<Channel> channel = CreateChannel(server_name, creds);
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

```cpp
std::shared_ptr<ChannelCredentials> creds = GoogleDefaultCredentials();
// Create a channel, stub and make RPC calls (same as in the previous example)
std::shared_ptr<Channel> channel = CreateChannel(server_name, creds);
std::unique_ptr<Greeter::Stub> stub(Greeter::NewStub(channel));
grpc::Status s = stub->sayHello(&context, *request, response);
```

This credential works for applications using Service Accounts as well as for
applications running in [Google Compute Engine (GCE)](https://cloud.google.com/compute/). In the former case, the
service account’s private keys are loaded from the file named in the environment
variable `GOOGLE_APPLICATION_CREDENTIALS`. The
keys are used to generate bearer tokens that are attached to each outgoing RPC
on the corresponding channel.

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

## Examples

These authentication mechanisms will be available in all gRPC's supported languages.
The following sections demonstrate how authentication and authorization features described above appear in each language: more languages are coming soon.

###SSL/TLS for server authentication and encryption (Ruby)
```ruby
# Base case - No encryption
stub = Helloworld::Greeter::Stub.new('localhost:50051')
...

# With server authentication SSL/TLS
creds = GRPC::Core::Credentials.new(load_certs)  # load_certs typically loads a CA roots file
stub = Helloworld::Greeter::Stub.new('localhost:50051', creds: creds)
```

###SSL/TLS for server authentication and encryption (C#)
```csharp
// Base case - No encryption
var channel = new Channel("localhost:50051");
var client = new Greeter.GreeterClient(channel);
...

// With server authentication SSL/TLS
var credentials = new SslCredentials(File.ReadAllText("ca.pem"));  // Load a CA file
var channel = new Channel("localhost:50051", credentials);
var client = new Greeter.GreeterClient(channel);
```

###SSL/TLS for server authentication and encryption (Objective-C)

The default for Objective-C is to use SSL/TLS, as that's the most common use case when accessing
remote APIs.

```objective-c
// Base case - With server authentication SSL/TLS
HLWGreeter *client = [[HLWGreeter alloc] initWithHost:@"localhost:50051"];
// Same as using @"https://localhost:50051".
...

// No encryption
HLWGreeter *client = [[HLWGreeter alloc] initWithHost:@"http://localhost:50051"];
// Specifying the HTTP scheme explicitly forces no encryption.
```

###SSL/TLS for server authentication and encryption (Python)
```python
# Base case - No encryption
stub = early_adopter_create_GreeterService_stub('localhost', 50051)
...

# With server authentication SSL/TLS
stub = early_adopter_create_GreeterService_stub(
  'localhost', 50051, secure=True, root_certificates=open('ca.pem').read())
...
```
n.b.: the beta API will look different

###Authenticating with Google (Ruby)
```ruby
# Base case - No encryption/authorization
stub = Helloworld::Greeter::Stub.new('localhost:50051')
...

# Authenticating with Google
require 'googleauth'  # from http://www.rubydoc.info/gems/googleauth/0.1.0
...
creds = GRPC::Core::Credentials.new(load_certs)  # load_certs typically loads a CA roots file
scope = 'https://www.googleapis.com/auth/grpc-testing'
authorization = Google::Auth.get_application_default(scope)
stub = Helloworld::Greeter::Stub.new('localhost:50051',
                                     creds: creds,
                                     update_metadata: authorization.updater_proc)
```

###Authenticating with Google (Node.js)

```node
// Base case - No encryption/authorization
var stub = new helloworld.Greeter('localhost:50051');
...
// Authenticating with Google
var GoogleAuth = require('google-auth-library'); // from https://www.npmjs.com/package/google-auth-library
...
var creds = grpc.Credentials.createSsl(load_certs); // load_certs typically loads a CA roots file
var scope = 'https://www.googleapis.com/auth/grpc-testing';
(new GoogleAuth()).getApplicationDefault(function(err, auth) {
  if (auth.createScopeRequired()) {
    auth = auth.createScoped(scope);
  }
  var stub = new helloworld.Greeter('localhost:50051',
                                    {credentials: creds},
                                    grpc.getGoogleAuthDelegate(auth));
});
```

###Authenticating with Google (C#)
```csharp
// Base case - No encryption/authorization
var channel = new Channel("localhost:50051");
var client = new Greeter.GreeterClient(channel);
...

// Authenticating with Google
using Grpc.Auth;  // from Grpc.Auth NuGet package
...
var credentials = new SslCredentials(File.ReadAllText("ca.pem"));  // Load a CA file
var channel = new Channel("localhost:50051", credentials);

string scope = "https://www.googleapis.com/auth/grpc-testing";
var authorization = GoogleCredential.GetApplicationDefault();
if (authorization.IsCreateScopedRequired)
{
    authorization = credential.CreateScoped(new[] { scope });
}
var client = new Greeter.GreeterClient(channel,
        new StubConfiguration(OAuth2InterceptorFactory.Create(credential)));
```

###Authenticating with Google (PHP)
```php
// Base case - No encryption/authorization
$client = new helloworld\GreeterClient(
  new Grpc\BaseStub('localhost:50051', []));
...

// Authenticating with Google
// the environment variable "GOOGLE_APPLICATION_CREDENTIALS" needs to be set
$scope = "https://www.googleapis.com/auth/grpc-testing";
$auth = Google\Auth\ApplicationDefaultCredentials::getCredentials($scope);
$opts = [
  'credentials' => Grpc\Credentials::createSsl(file_get_contents('ca.pem'));
  'update_metadata' => $auth->getUpdateMetadataFunc(),
];

$client = new helloworld\GreeterClient(
  new Grpc\BaseStub('localhost:50051', $opts));

```

###Authenticating with Google (Objective-C)

This example uses the [Google iOS Sign-In library](https://developers.google.com/identity/sign-in/ios/),
but it's easily extrapolated to any other OAuth2 library.

```objective-c
// Base case - No authentication
[client sayHelloWithRequest:request handler:^(HLWHelloReply *response, NSError *error) {
  ...
}];

...

// Authenticating with Google

// When signing the user in, ask her for the relevant scopes.
GIDSignIn.sharedInstance.scopes = @[@"https://www.googleapis.com/auth/grpc-testing"];

...

#import <ProtoRPC/ProtoRPC.h>

// Create a not-yet-started RPC. We want to set the request headers on this object before starting
// it.
ProtoRPC *call =
    [client RPCToSayHelloWithRequest:request handler:^(HLWHelloReply *response, NSError *error) {
      ...
    }];

// Set the access token to be used.
NSString *accessToken = GIDSignIn.sharedInstance.currentUser.authentication.accessToken;
call.requestMetadata[@"Authorization"] = [@"Bearer " stringByAppendingString:accessToken]}];

// Start the RPC.
[call start];
```

You can see a working example app, with a more detailed explanation, [here](examples/objective-c/auth_sample).

### Authenticating with Google (Python)
```python
# Base case - No encryption
stub = early_adopter_create_GreeterService_stub('localhost', 50051)
...

# With server authentication SSL/TLS
import oauth2client.client
credentials = oauth2client.GoogleCredentials.get_application_default()
scope = 'https://www.googleapis.com/auth/grpc-testing'
scoped_credentials = credentials.create_scoped([scope])
access_token = scoped_credentials.get_access_token().access_token
metadata_transformer = (
    lambda x: [('Authorization', 'Bearer {}'.format(access_token))])

stub = early_adopter_create_GreeterService_stub(
  'localhost', 50051, secure=True, root_certificates=open('ca.pem').read(),
  metadata_transformer=metadata_transformer)
...
```
n.b.: the beta API will look different
