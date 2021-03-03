FROM phusion/baseimage:master

RUN apt-get update -y && \
        apt-get install -y \
            build-essential \
            clang \
            python3 \
            python3-dev
RUN ln -s /usr/bin/python3 /usr/bin/python
COPY . .
RUN tools/bazel build -c dbg //src/python/grpcio_tests/tests_py3_only/interop:xds_interop_client

ENTRYPOINT ["bazel-bin/src/python/grpcio_tests/tests_py3_only/interop/xds_interop_client"]
