FROM ubuntu:14.04

RUN apt-get -y update && \
    apt-get install -y curl git-core mingw-w64 xz-utils build-essential gcc-multilib wget unzip

RUN mkdir -p /opt/mingw && \
    curl -SL http://downloads.sourceforge.net/mingw-w64/i686-w64-mingw32-gcc-4.7.2-release-linux64_rubenvb.tar.xz | \
    tar -xJC /opt/mingw && \
    echo "export PATH=\$PATH:/opt/mingw/mingw32/bin" >> /etc/rubybashrc

RUN mkdir -p /opt/mingw && \
    curl -SL http://downloads.sourceforge.net/mingw-w64/x86_64-w64-mingw32-gcc-4.7.2-release-linux64_rubenvb.tar.xz | \
    tar -xJC /opt/mingw && \
    echo "export PATH=\$PATH:/opt/mingw/mingw64/bin" >> /etc/rubybashrc

# Add "rvm" as system group, to avoid conflicts with host GIDs typically starting with 1000
RUN groupadd -r rvm && useradd -r -g rvm -G sudo -p "" --create-home rvm && \
    echo "source /etc/profile.d/rvm.sh" >> /etc/rubybashrc
USER rvm

# install rvm, RVM 1.26.0+ has signed releases, source rvm for usage outside of package scripts
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys D39DC0E3 && \
    (curl -L http://get.rvm.io | sudo bash -s stable) && \
    bash -c " \
        source /etc/rubybashrc && \
        rvmsudo rvm cleanup all "

# Regenerate the following using build.sh if the build folder changes.
RUN echo \
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\
| base64 -d | tar xzC /tmp

# Import patch files for ruby and gems
RUN cp -r /tmp/build/patches /home/rvm/patches/
ENV BASH_ENV /etc/rubybashrc

# install rubies and fix permissions on
RUN bash -c " \
    export CFLAGS='-s -O3 -fno-fast-math -fPIC' && \
    for v in 2.3.0 ; do \
        rvm install \$v --patch \$(echo ~/patches/ruby-\$v/* | tr ' ' ','); \
    done && \
    rvm cleanup all && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

# Install rake-compiler and typical gems in all Rubies
# do not generate documentation for gems
RUN echo "gem: --no-ri --no-rdoc" >> ~/.gemrc && \
    bash -c " \
        rvm all do gem install bundler rake-compiler hoe mini_portile rubygems-tasks json && \
        rvm 2.3.0 do gem install mini_portile2 && \
        find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

# Install rake-compiler's cross rubies in global dir instead of /root
RUN sudo mkdir -p /usr/local/rake-compiler && \
    sudo chown rvm.rvm /usr/local/rake-compiler && \
    ln -s /usr/local/rake-compiler ~/.rake-compiler

# Patch rake-compiler to avoid build of ruby extensions
RUN cd /usr/local/rvm/gems/ruby-2.3.0/gems/rake-compiler-0.9.5 && patch -p1 < /home/rvm/patches/rake-compiler-0.9.5/without-exts.diff ; \
    true

RUN bash -c "rvm use 2.3.0 --default && \
    export MAKE=\"make -j`nproc`\" CFLAGS='-s -O1 -fno-omit-frame-pointer -fno-fast-math' && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.1.6 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.1.6 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.1.6 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=x86_64-linux-gnu && \
    rm -rf ~/.rake-compiler/tmp/builds ~/.rake-compiler/sources && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

RUN bash -c "rvm use 2.3.0 --default && \
    export MAKE=\"make -j`nproc`\" CFLAGS='-m32 -s -O1 -fno-omit-frame-pointer -fno-fast-math' LDFLAGS='-m32' && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.1.6 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=i686-linux-gnu && \
    rm -rf ~/.rake-compiler/tmp/builds ~/.rake-compiler/sources && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

RUN bash -c " \
    rvm alias create 2.3 2.3.0 "

USER root

# Fix paths in rake-compiler/config.yml and add rvm and mingw-tools to the global bashrc
RUN sed -i -- "s:/root/.rake-compiler:/usr/local/rake-compiler:g" /usr/local/rake-compiler/config.yml && \
    echo "source /etc/profile.d/rvm.sh" >> /etc/bash.bashrc && \
    echo "export PATH=\$PATH:/opt/mingw/mingw32/bin" >> /etc/bash.bashrc && \
    echo "export PATH=\$PATH:/opt/mingw/mingw64/bin" >> /etc/bash.bashrc

# Install wrappers for strip commands as a workaround for "Protocol error" in boot2docker.
RUN cp /tmp/build/strip_wrapper /root/
RUN mv /opt/mingw/mingw32/bin/i686-w64-mingw32-strip /opt/mingw/mingw32/bin/i686-w64-mingw32-strip.bin && \
    mv /opt/mingw/mingw64/bin/x86_64-w64-mingw32-strip /opt/mingw/mingw64/bin/x86_64-w64-mingw32-strip.bin && \
    mv /usr/bin/i686-w64-mingw32-strip /usr/bin/i686-w64-mingw32-strip.bin && \
    mv /usr/bin/x86_64-w64-mingw32-strip /usr/bin/x86_64-w64-mingw32-strip.bin && \
    ln /root/strip_wrapper /opt/mingw/mingw32/bin/i686-w64-mingw32-strip && \
    ln /root/strip_wrapper /opt/mingw/mingw64/bin/x86_64-w64-mingw32-strip && \
    ln /root/strip_wrapper /usr/bin/i686-w64-mingw32-strip && \
    ln /root/strip_wrapper /usr/bin/x86_64-w64-mingw32-strip

RUN find / -name rbconfig.rb | while read f ; do sed -i 's/0x0501/0x0600/' $f ; done
RUN find / -name win32.h | while read f ; do sed -i 's/gettimeofday/rb_gettimeofday/' $f ; done
RUN sed -i 's/defined.__MINGW64__.$/1/' /usr/local/rake-compiler/ruby/i686-w64-mingw32/ruby-2.0.0-p645/include/ruby-2.0.0/ruby/win32.h
RUN find / -name libwinpthread.dll.a | while read f ; do rm $f ; done
RUN find / -name libwinpthread-1.dll | while read f ; do rm $f ; done
RUN find / -name *msvcrt-ruby*.dll.a | while read f ; do n=`echo $f | sed s/.dll//` ; mv $f $n ; done

# Install SIGINT forwarder
RUN cp /tmp/build/sigfw.c /root/
RUN gcc $HOME/sigfw.c -o /usr/local/bin/sigfw

# Install user mapper
RUN cp /tmp/build/runas /usr/local/bin/

# Install sudoers configuration
RUN cp /tmp/build/sudoers /etc/sudoers.d/rake-compiler-dock

ENV RUBY_CC_VERSION 2.3.0:2.2.2:2.1.6:2.0.0

CMD bash
