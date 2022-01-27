# Authentication Extension Example in gRPC Python

## Check Our Guide First

For most common usage of authentication in gRPC Python, please see our
[Authentication](https://grpc.io/docs/guides/auth/) guide's Python section. The
Guide includes following scenarios:

1. Server SSL credential setup
2. Client SSL credential setup
3. Authenticate with Google using a JWT
4. Authenticate with Google using an Oauth2 token

Also, the guide talks about gRPC specific credential types.

### Channel credentials

Channel credentials are attached to a `Channel` object, the most common use case
are SSL credentials.

### Call credentials

Call credentials are attached to a `Call` object (corresponding to an RPC).
Under the hood, the call credentials is a function that takes in information of
the RPC and modify metadata through callback.

## About This Example

This example focuses on extending gRPC authentication mechanism:
1) Customize authentication plugin;
2) Composite client side credentials;
3) Validation through interceptor on server side.

## AuthMetadataPlugin: Manipulate metadata for each call

Unlike TLS/SSL based authentication, the authentication extension in gRPC Python
lives at a much higher level of networking. It relies on the transmission of
metadata (HTTP Header) between client and server, instead of alternating the
transport protocol.

gRPC Python provides a way to intercept an RPC and append authentication related
metadata through
[`AuthMetadataPlugin`](https://grpc.github.io/grpc/python/grpc.html#grpc.AuthMetadataPlugin).
Those in need of a custom authentication method may simply provide a concrete
implementation of the following interface:

```Python
class AuthMetadataPlugin:
    """A specification for custom authentication."""

    def __call__(self, context, callback):
        """Implements authentication by passing metadata to a callback.

        Implementations of this method must not block.

        Args:
          context: An AuthMetadataContext providing information on the RPC that
            the plugin is being called to authenticate.
          callback: An AuthMetadataPluginCallback to be invoked either
            synchronously or asynchronously.
        """
```

Then pass the instance of the concrete implementation to
`grpc.metadata_call_credentials` function to be converted into a
`CallCredentials` object. Please NOTE that it is possible to pass a Python
function object directly, but we recommend to inherit from the base class to
ensure implementation correctness.


```Python
def metadata_call_credentials(metadata_plugin, name=None):
    """Construct CallCredentials from an AuthMetadataPlugin.

    Args:
      metadata_plugin: An AuthMetadataPlugin to use for authentication.
      name: An optional name for the plugin.

    Returns:
      A CallCredentials.
    """
```

The `CallCredentials` object can be passed directly into an RPC like:

```Python
call_credentials = grpc.metadata_call_credentials(my_foo_plugin)
stub.FooRpc(request, credentials=call_credentials)
```

Or you can use `ChannelCredentials` and `CallCredentials` at the same time by
combining them:

```Python
channel_credentials = ...
call_credentials = ...
composite_credentials = grpc.composite_channel_credentials(
    channel_credential,
    call_credentials)
channel = grpc.secure_channel(server_address, composite_credentials)
```

It is also possible to apply multiple `CallCredentials` to a single RPC:

```Python
call_credentials_foo = ...
call_credentials_bar = ...
call_credentials = grpc.composite_call_credentials(
    call_credentials_foo,
    call_credentials_bar)
stub.FooRpc(request, credentials=call_credentials)
```
