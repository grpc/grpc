FROM dockcross/manylinux2010-x86

# Docker file for building gRPC manylinux-based Ruby artifacts.
# Updated: 2020-07-03

# install packages which rvm will require
RUN yum install -y autoconf gcc-c++ libtool readline-devel ruby sqlite-devel openssl-devel xz

# install rvm, RVM 1.26.0+ has signed releases, source rvm for usage outside of package scripts
RUN gpg --keyserver hkp://pool.sks-keyservers.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3 7D2BAF1CF37B13E2069D6956105BD0E739499BDB && \
    (curl -L http://get.rvm.io | sudo bash)
RUN echo "source /etc/profile.d/rvm.sh" >> /etc/rubybashrc
RUN bash -c " \
        source /etc/rubybashrc && \
        rvm cleanup all "

# Import patch files for ruby and gems
COPY build/patches /work/patches/
ENV BASH_ENV /etc/rubybashrc

# install rubies and fix permissions on
RUN bash -c " \
    export CFLAGS='-s -O3 -fno-fast-math -fPIC' && \
    for v in 2.5.7 ; do \
        rvm install \$v --patch \$(echo /work/patches/ruby-\$v/* | tr ' ' ','); \
    done && \
    rvm cleanup all && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

# Install rake-compiler and typical gems in all Rubies
# do not generate documentation for gems
RUN echo "gem: --no-ri --no-rdoc" >> ~/.gemrc && \
    bash -c " \
        rvm all do gem update --system --no-document && \
        rvm all do gem install --no-document bundler 'bundler:~>1.16' rake-compiler:1.1.0 hoe:3.20.0 mini_portile rubygems-tasks mini_portile2 && \
        find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

# Install rake-compiler's cross rubies in global dir instead of /root
RUN mkdir -p /usr/local/rake-compiler && \
    ln -s /usr/local/rake-compiler ~/.rake-compiler

RUN bash -c " \
        rvm alias create default 2.5.7 && \
        rvm use default "

# Patch rake-compiler and hoe package
COPY build/patches2 /work/patches/
RUN cd /usr/local/rvm/gems/ruby-2.5.7/gems/rake-compiler-1.1.0 && \
    ( git apply /work/patches/rake-compiler-1.1.0/*.patch || true )
RUN cd /usr/local/rvm/gems/ruby-2.5.7/gems/hoe-3.20.0 && \
    ( git apply /work/patches/hoe-3.20.0/*.patch || true )

# Patch ruby-2.7.0 for cross build
RUN curl -SL http://cache.ruby-lang.org/pub/ruby/2.7/ruby-2.7.0.tar.xz | tar -xJC /root/ && \
    cd /root/ruby-2.7.0 && \
    git apply /work/patches/ruby-2.7.0/*.patch && \
    cd .. && \
    mkdir -p /usr/local/rake-compiler/sources/ && \
    tar cjf /usr/local/rake-compiler/sources/ruby-2.7.0.tar.bz2 ruby-2.7.0 && \
    rm -rf /root/ruby-2.7.0

ENV XRUBIES 2.7.0:2.6.0:2.5.0:2.4.0:2.3.0:2.2.2

# Build all xruby versions in parallel
# Then cleanup all build artifacts
RUN bash -c " \
    export CFLAGS='-s -O1 -fno-omit-frame-pointer -fno-fast-math' && \
    export MAKE='make V=0' && \
    rake-compiler cross-ruby VERSION=$XRUBIES HOST=x86-linux-gnu && \
    rm -rf ~/.rake-compiler/builds ~/.rake-compiler/sources && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

# Avoid linking against libruby shared object.
# See also https://github.com/rake-compiler/rake-compiler-dock/issues/13
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name libruby.so | xargs rm
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name libruby-static.a | while read f ; do cp $f `echo $f | sed s/-static//` ; done
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name libruby.a | while read f ; do ar t $f | xargs ar d $f ; done
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name mkmf.rb | while read f ; do sed -i ':a;N;$!ba;s/TRY_LINK = [^\n]*\n[^\n]*\n[^\n]*LOCAL_LIBS)/& -lruby-static/' $f ; done

# Fix paths in rake-compiler/config.yml and add rvm and mingw-tools to the global bashrc
RUN sed -i -- "s:/root/.rake-compiler:/usr/local/rake-compiler:g" /usr/local/rake-compiler/config.yml && \
    echo "source /etc/profile.d/rvm.sh" >> /etc/bash.bashrc

# Install SIGINT forwarder
COPY build/sigfw.c /root/
RUN gcc $HOME/sigfw.c -o /usr/local/bin/sigfw

# Install patchelf_gem.sh
COPY build/patchelf_gem.sh /usr/local/bin/patchelf_gem.sh

ENV RUBY_CC_VERSION 2.7.0:2.6.0:2.5.0:2.4.0:2.3.0:2.2.2
