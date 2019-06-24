# Authentication Extension Example in gRPC Python

## Check Our Guide First

For most common usage of authentication in gRPC Python, please see our [Authentication](https://grpc.io/docs/guides/auth/) guide's Python section, it includes:

1. Server SSL credential setup
2. Client SSL credential setup
3. Authenticate with Google using a JWT
4. Authenticate with Google using an Oauth2 token

Also, the guide talks about gRPC specific credential types.

### Channel credentials

Channel credentials are attached to a `Channel` object, the most common use case are SSL credentials.

### Call credentials

Call credentials are attached to a `Call` object (corresponding to an RPC). Under the hood, the call credentials is a function that takes in information of the RPC and modify metadata through callback.

## About This Example

This example focuses on extending gRPC authentication mechanism:
1) Customize authentication plugin;
2) Composite client side credentials;
3) Validation through interceptor on server side.

## AuthMetadataPlugin: Manipulate metadata for each call

Unlike TLS/SSL based authentication, the authentication extension in gRPC Python lives in a much higher level of abstraction. It relies on the transmit of metadata (HTTP Header) between client and server. gRPC Python provides a way to intercept an RPC and append authentication related metadata through [`AuthMetadataPlugin`](https://grpc.github.io/grpc/python/grpc.html#grpc.AuthMetadataPlugin).

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
