FROM ubuntu:17.04

RUN apt-get -y update && \
    apt-get install -y curl git-core xz-utils build-essential wget unzip sudo gpg dirmngr

# Add "rvm" as system group, to avoid conflicts with host GIDs typically starting with 1000
RUN groupadd -r rvm && useradd -r -g rvm -G sudo -p "" --create-home rvm && \
    echo "source /etc/profile.d/rvm.sh" >> /etc/rubybashrc

USER root
RUN apt-get -y update && \
    apt-get install -y gcc-mingw-w64-x86-64 gcc-mingw-w64-i686 g++-mingw-w64-x86-64 g++-mingw-w64-i686 \
        gcc-multilib moreutils
USER rvm

# install rvm, RVM 1.26.0+ has signed releases, source rvm for usage outside of package scripts
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys D39DC0E3 && \
    (curl -L http://get.rvm.io | sudo bash -s stable) && \
    bash -c " \
        source /etc/rubybashrc && \
        rvmsudo rvm cleanup all "

# Regenerate the following using build.sh if the build folder changes.
RUN echo \\
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\
| base64 -d | tar xzC /tmp

# Import patch files for ruby and gems
RUN cp -r /tmp/build/patches /home/rvm/patches/
ENV BASH_ENV /etc/rubybashrc

# install rubies and fix permissions on
RUN bash -c " \
    export CFLAGS='-s -O3 -fno-fast-math -fPIC' && \
    echo 'about to install patches for ruby 2.4.0 from:' && \
    ls -r ~/patches && \
    for v in 2.4.0 ; do \
	rvm install \$v --patch \$(echo ~/patches/ruby-\$v/* | tr ' ' ','); \
    done && \
    rvm cleanup all && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

# Install rake-compiler and typical gems in all Rubies
# do not generate documentation for gems
RUN echo "gem: --no-ri --no-rdoc" >> ~/.gemrc && \
    bash -c " \
        rvm all do gem install bundler rake-compiler hoe mini_portile rubygems-tasks && \
        rvm 2.4.0 do gem install mini_portile2 && \
        find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

RUN bash -c "gem env"
RUN bash -c "gem list rake-compiler"

# Install rake-compiler's cross rubies in global dir instead of /root
RUN sudo mkdir -p /usr/local/rake-compiler && \
    sudo chown rvm.rvm /usr/local/rake-compiler && \
    ln -s /usr/local/rake-compiler ~/.rake-compiler

# Patch rake-compiler to avoid build of ruby extensions
RUN cd /usr/local/rvm/gems/ruby-2.4.0/gems/rake-compiler-0.9.5 && git apply /home/rvm/patches/rake-compiler-0.9.5/*.diff ; \
    true

RUN bash -c "rvm use 2.4.0 --default && \
    export MAKE=\"make -j`nproc`\" CFLAGS='-s -O1 -fno-omit-frame-pointer -fno-fast-math' && \
    rake-compiler cross-ruby VERSION=2.4.0 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.4.0 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.4.0 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.1.5 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.1.5 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.1.5 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=x86_64-linux-gnu && \
    rm -rf ~/.rake-compiler/tmp/builds ~/.rake-compiler/sources && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

RUN bash -c "rvm use 2.4.0 --default && \
    export MAKE=\"make -j`nproc`\" CFLAGS='-m32 -s -O1 -fno-omit-frame-pointer -fno-fast-math' LDFLAGS='-m32' && \
    rake-compiler cross-ruby VERSION=2.4.0 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.1.5 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=i686-linux-gnu && \
    rm -rf ~/.rake-compiler/tmp/builds ~/.rake-compiler/sources && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

RUN bash -c " \
    rvm alias create 2.4 2.4.0 "

USER root

# Fix paths in rake-compiler/config.yml and add rvm and mingw-tools to the global bashrc
RUN sed -i -- "s:/root/.rake-compiler:/usr/local/rake-compiler:g" /usr/local/rake-compiler/config.yml && \
    echo "source /etc/profile.d/rvm.sh" >> /etc/bash.bashrc && \
    echo "export PATH=\$PATH:/opt/mingw/mingw32/bin" >> /etc/bash.bashrc && \
    echo "export PATH=\$PATH:/opt/mingw/mingw64/bin" >> /etc/bash.bashrc

# Install wrappers for strip commands as a workaround for "Protocol error" in boot2docker.
RUN cp /tmp/build/strip_wrapper /root/
RUN sudo chmod +rx /root/strip_wrapper
RUN mv /usr/bin/i686-w64-mingw32-strip /usr/bin/i686-w64-mingw32-strip.bin && \
    mv /usr/bin/x86_64-w64-mingw32-strip /usr/bin/x86_64-w64-mingw32-strip.bin && \
    ln /root/strip_wrapper /usr/bin/i686-w64-mingw32-strip && \
    ln /root/strip_wrapper /usr/bin/x86_64-w64-mingw32-strip

RUN find / -name rbconfig.rb | while read f ; do sed -i 's/0x0501/0x0600/' $f ; done
RUN find / -name win32.h | while read f ; do sed -i 's/gettimeofday/rb_gettimeofday/' $f ; done
RUN sed -i 's/defined.__MINGW64__.$/1/' /usr/local/rake-compiler/ruby/i686-w64-mingw32/ruby-2.0.0-p645/include/ruby-2.0.0/ruby/win32.h
RUN find / -name libwinpthread.dll.a | xargs rm
RUN find / -name libwinpthread-1.dll | xargs rm
RUN find / -name *msvcrt-ruby*.dll.a | while read f ; do n=`echo $f | sed s/.dll//` ; mv $f $n ; done
RUN find /usr/local/rake-compiler/ruby -name libruby.so | xargs rm
RUN find /usr/local/rake-compiler/ruby -name libruby-static.a | while read f ; do ar t $f | xargs ar d $f ; done
RUN find /usr/local/rake-compiler/ruby -name libruby-static.a | while read f ; do mv $f `echo $f | sed s/-static//` ; done

# Install SIGINT forwarder
RUN cp /tmp/build/sigfw.c /root/
RUN gcc $HOME/sigfw.c -o /usr/local/bin/sigfw

# Install user mapper
RUN cp /tmp/build/runas /usr/local/bin/

# Install sudoers configuration
RUN cp /tmp/build/sudoers /etc/sudoers.d/rake-compiler-dock

# Fixup Ruby 2.4 'static' compilation issue.
RUN echo '!<arch>' > /usr/local/rake-compiler/ruby/x86_64-linux-gnu/ruby-2.4.0/lib/libruby.a
RUN echo '!<arch>' > /usr/local/rake-compiler/ruby/i686-linux-gnu/ruby-2.4.0/lib/libruby.a

ENV RUBY_CC_VERSION 2.4.0:2.3.0:2.2.2:2.1.5:2.0.0

RUN apt-get install -y g++-multilib

CMD bash
