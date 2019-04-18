This directory contains some grpc-ruby end to end tests.

Each test here involves two files: a "driver" and a "client". For example,
the "channel_closing" test involves channel_closing_driver.rb
and channel_closing_client.rb.

Typically, the "driver" will start up a simple "echo" server, and then
spawn a client. It gives the client the address of the "echo" server as
well as an address to listen on for control rpcs. Depending on the test, the
client usually starts up a "ClientControl" grpc server for the driver to
interact with (the driver can tell the client process to do strange things at
different times, depending on the test).

So far these tests are mostly useful for testing process-shutdown related
situations, since the client's run in separate processes.

These tests are invoked through the "tools/run_tests/run_tests.py" script (the
Rakefile doesn't start these).
