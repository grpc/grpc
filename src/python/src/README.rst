gRPC Python
===========

Package for GRPC Python.

Dependencies
------------

Ensure that you have installed GRPC core.

On debian linux systems, install from our released deb package:

::

  $ wget https://github.com/grpc/grpc/releases/download/release-0_5_0/libgrpc_0.5.0_amd64.deb
  $ wget https://github.com/grpc/grpc/releases/download/release-0_5_0/libgrpc-dev_0.5.0_amd64.deb
  $ sudo dpkg -i libgrpc_0.5.0_amd64.deb libgrpc-dev_0.5.0_amd64.deb

Otherwise, install from source:

::

  git clone https://github.com/grpc/grpc.git
  cd grpc
  ./configure
  make && make install

