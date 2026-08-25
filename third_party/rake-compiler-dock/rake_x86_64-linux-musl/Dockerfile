FROM us-docker.pkg.dev/grpc-testing/testing-images-public/rake-compiler-dock-image:1.12.0-mri-x86_64-linux-musl@sha256:dbbf133911aa0a92d749e96ddde7ec1613608fe726692d90df878497a4bdf2a2

#=================
# Install ccache

# Install ccache from source since ccache 3.x packaged with most linux distributions
# does not support Redis backend for caching.
RUN curl -sSL -o ccache.tar.gz https://github.com/ccache/ccache/releases/download/v4.5.1/ccache-4.5.1.tar.gz \
    && tar -zxf ccache.tar.gz \
    && cd ccache-4.5.1 \
    && mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DZSTD_FROM_INTERNET=ON -DHIREDIS_FROM_INTERNET=ON .. \
    && make -j4 && make install \
    && cd ../.. \
    && rm -rf ccache-4.5.1 ccache.tar.gz

