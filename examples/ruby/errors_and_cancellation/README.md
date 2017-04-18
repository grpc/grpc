# Errors and Cancelletion code samples for grpc-ruby

The examples in this directory show use of grpc errors.

On the server side, errors are returned from service
implementations by raising a certain `GRPC::BadStatus` exception.

On the client side, GRPC errors get raised when either:
 * the call completes (unary and client-streaming call types)
 * the response `Enumerable` is iterated through (server-streaming and
   bidi call types).

## To run the examples here:

Start the server:

```
> ruby error_examples_server.rb
```

Then run the client:

```
> ruby error_examples_client.rb
```
