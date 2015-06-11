gRPC Python
===========

Package for GRPC Python.

Dependencies
------------

Ensure you have installed the gRPC core.  On Mac OS X, install homebrew_. On Linux, install linuxbrew_.
Run the following command to install gRPC Python.

::

  $ curl -fsSL https://goo.gl/getgrpc | bash -s python

This will download and run the [gRPC install script][] to install grpc core. The script then uses pip to install this package.  It also installs the Protocol Buffers compiler (_protoc_) and the gRPC _protoc_ plugin for python.

Otherwise, `install from source`_

.. _`install from source`: https://github.com/grpc/grpc/blob/master/src/python/README.md#building-from-source
.. _homebrew: http://brew.sh
.. _linuxbrew: https://github.com/Homebrew/linuxbrew#installation
.. _`gRPC install script`: https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
