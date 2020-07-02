FROM dockcross/manylinux2010-x64

# update sudo
COPY build/sudo.sh /usr/local/bin/sudo
RUN chmod +rx /usr/local/bin/sudo

# open sudo to everyone (https://github.com/tianon/gosu/issues/11)
RUN chmod +s /usr/local/bin/gosu

# install packages which rvm will require
RUN yum install -y autoconf gcc-c++ libtool readline-devel ruby sqlite-devel openssl-devel xz

# Add "rvm" as system group, to avoid conflicts with host GIDs typically starting with 1000
RUN groupadd -r rvm && useradd -r -g rvm -G wheel -p "" --create-home rvm && \
    echo "source /etc/profile.d/rvm.sh" >> /etc/rubybashrc
USER rvm

RUN mkdir ~/.gnupg && \
    chmod 700 ~/.gnupg && \
    echo "disable-ipv6" >> ~/.gnupg/dirmngr.conf

# install rvm, RVM 1.26.0+ has signed releases, source rvm for usage outside of package scripts
RUN gpg --keyserver hkp://pool.sks-keyservers.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3 7D2BAF1CF37B13E2069D6956105BD0E739499BDB && \
    (curl -L http://get.rvm.io | sudo bash)
RUN bash -c " \
        source /etc/rubybashrc && \
        sudo rvm cleanup all "

# Import patch files for ruby and gems
COPY build/patches /home/rvm/patches/
ENV BASH_ENV /etc/rubybashrc

# install rubies and fix permissions on
RUN bash -c " \
    export CFLAGS='-s -O3 -fno-fast-math -fPIC' && \
    for v in 2.5.7 ; do \
        rvm install \$v --patch \$(echo ~/patches/ruby-\$v/* | tr ' ' ','); \
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
RUN sudo mkdir -p /usr/local/rake-compiler && \
    sudo chown rvm.rvm /usr/local/rake-compiler && \
    ln -s /usr/local/rake-compiler ~/.rake-compiler

USER root

RUN bash -c " \
        rvm alias create default 2.5.7 && \
        rvm use default "

# Create dev tools x86-linux-*
COPY build/mk_i686.rb /root/
RUN bash -c " \
        ruby /root/mk_i686.rb "

USER rvm

COPY build/patches2 /home/rvm/patches/
# Patch rake-compiler to build and install static libraries for Linux rubies
RUN cd /usr/local/rvm/gems/ruby-2.5.7/gems/rake-compiler-1.1.0 && \
    ( git apply /home/rvm/patches/rake-compiler-1.1.0/*.patch || true )
RUN cd /usr/local/rvm/gems/ruby-2.5.7/gems/hoe-3.20.0 && \
    ( git apply /home/rvm/patches/hoe-3.20.0/*.patch || true )

# Patch ruby-2.7.0 for cross build
USER root
RUN curl -SL http://cache.ruby-lang.org/pub/ruby/2.7/ruby-2.7.0.tar.xz | tar -xJC /root/ && \
    cd /root/ruby-2.7.0 && \
    git apply /home/rvm/patches/ruby-2.7.0/*.patch && \
    cd .. && \
    mkdir -p /usr/local/rake-compiler/sources/ && \
    tar cjf /usr/local/rake-compiler/sources/ruby-2.7.0.tar.bz2 ruby-2.7.0 && \
    chown rvm /usr/local/rake-compiler -R && \
    rm -rf /root/ruby-2.7.0
USER rvm

ENV XRUBIES 2.7.0:2.6.0:2.5.0:2.4.0:2.3.0:2.2.2

# Build all xruby versions in parallel
# Then cleanup all build artifacts
RUN bash -c " \
    export CFLAGS='-s -O1 -fno-omit-frame-pointer -fno-fast-math' && \
    export MAKE='make V=0' && \
    rake-compiler cross-ruby VERSION=$XRUBIES HOST=x86_64-linux-gnu && \
    rm -rf ~/.rake-compiler/builds ~/.rake-compiler/sources && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

# Avoid linking against libruby shared object.
# See also https://github.com/rake-compiler/rake-compiler-dock/issues/13
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name libruby.so | xargs rm
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name libruby-static.a | while read f ; do cp $f `echo $f | sed s/-static//` ; done
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name libruby.a | while read f ; do ar t $f | xargs ar d $f ; done
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name mkmf.rb | while read f ; do sed -i ':a;N;$!ba;s/TRY_LINK = [^\n]*\n[^\n]*\n[^\n]*LOCAL_LIBS)/& -lruby-static/' $f ; done

USER root

# Fix paths in rake-compiler/config.yml and add rvm and mingw-tools to the global bashrc
RUN sed -i -- "s:/root/.rake-compiler:/usr/local/rake-compiler:g" /usr/local/rake-compiler/config.yml && \
    echo "source /etc/profile.d/rvm.sh" >> /etc/bash.bashrc

# Install SIGINT forwarder
COPY build/sigfw.c /root/
RUN gcc $HOME/sigfw.c -o /usr/local/bin/sigfw

# Install user mapper;
COPY build/runas /usr/local/bin/

# Install sudoers configuration
COPY build/sudoers /etc/sudoers.d/rake-compiler-dock

ENV RUBY_CC_VERSION 2.7.0:2.6.0:2.5.0:2.4.0:2.3.0:2.2.2

CMD bash
