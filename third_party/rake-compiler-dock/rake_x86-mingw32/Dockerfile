FROM us-docker.pkg.dev/grpc-testing/testing-images-public/rake_x86-mingw32:8bdaa700cd4d5f576ed18a84802359d0e69ea7a9@sha256:00e5426b71a1973a5c0b266ab8a154172688fd648320107b6c5679b134362dce

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
