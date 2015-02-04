GRPC PHP Dockerfile
===================

Dockerfile for creating the PHP development instances

As of 2014/10 this
- is based on the GRPC PHP base
- adds a pull of the HEAD GRPC PHP source from GitHub
- it builds it
- runs the tests, i.e, the image won't be created if the tests don't pass
