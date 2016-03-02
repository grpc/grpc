gRPC Python
===========

Package for gRPC Python.

Installation
------------

gRPC Python is available for Linux, Mac OS X, and Windows running Python 2.7.

From PyPI
~~~~~~~~~

If you are installing locally...

::

  $ pip install grpcio

Else system wide (on Ubuntu)...

::

  $ sudo pip install grpcio

n.b. On Windows and on Mac OS X one *must* have a recent release of :code:`pip`
to retrieve the proper wheel from PyPI. Be sure to upgrade to the latest
version!

From Source
~~~~~~~~~~~

Building from source requires that you have the Python headers (usually a
package named :code:`python-dev`).

::

  $ export REPO_ROOT=grpc
  $ git clone https://github.com/grpc/grpc.git $REPO_ROOT
  $ cd $REPO_ROOT
  $ pip install .

Note that :code:`$REPO_ROOT` can be assigned to whatever directory name floats
your fancy.
