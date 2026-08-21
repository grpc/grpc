FROM us-docker.pkg.dev/grpc-testing/testing-images-public/rake-compiler-dock-image:1.12.0-mri-x64-mingw-ucrt@sha256:6d5f60ed0ab1f039ad0ca2f2e738498cc577425b9978b479e5be6b0004f551ab

RUN find / -name win32.h | while read f ; do sed -i 's/gettimeofday/rb_gettimeofday/' $f ; done

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
