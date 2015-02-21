# Dockerfile for GRPC Python
FROM grpc/python_base

# Build the C library
RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

# Build the C core.
RUN make install_c -C /var/local/git/grpc

# Build Python GRPC
RUN cd /var/local/git/grpc \
  && pip install src/python/src \
  && pip install src/python/interop

# Run Python GRPC's tests
RUN cd /var/local/git/grpc \
  # TODO(nathaniel): It would be nice for these to be auto-discoverable?
  && python2.7 -B -m grpc._adapter._blocking_invocation_inline_service_test
  && python2.7 -B -m grpc._adapter._c_test
  && python2.7 -B -m grpc._adapter._event_invocation_synchronous_event_service_test
  && python2.7 -B -m grpc._adapter._future_invocation_asynchronous_event_service_test
  && python2.7 -B -m grpc._adapter._links_test
  && python2.7 -B -m grpc._adapter._lonely_rear_link_test
  && python2.7 -B -m grpc._adapter._low_test
  && python2.7 -B -m grpc.framework.assembly.implementations_test
  && python2.7 -B -m grpc.framework.base.packets.implementations_test
  && python2.7 -B -m grpc.framework.face.blocking_invocation_inline_service_test
  && python2.7 -B -m grpc.framework.face.event_invocation_synchronous_event_service_test
  && python2.7 -B -m grpc.framework.face.future_invocation_asynchronous_event_service_test
  && python2.7 -B -m grpc.framework.foundation._later_test
  && python2.7 -B -m grpc.framework.foundation._logging_pool_test

# Add a cacerts directory containing the Google root pem file, allowing the interop client to access the production test instance
ADD cacerts cacerts

# Specify the default command such that the interop server runs on its known testing port
CMD ["/bin/bash", "-l", "-c", "python2.7 -m interop.server --use_tls --port 8050"]
