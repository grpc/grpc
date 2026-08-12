FROM ubuntu:20.04
ARG TARGETPLATFORM

##
## Install required base packages for compiling ruby
##
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get -y update && \
    apt-get install -y sudo wget curl git-core build-essential xz-utils unzip dirmngr && \
    apt-get install -y autoconf cmake pkg-config zlib1g-dev libreadline-dev libsqlite0-dev libssl-dev libyaml-dev libffi-dev && \
    rm -rf /var/lib/apt/lists/*

##
##  install rbenv and ruby-build
##
RUN groupadd -r rubyuser && useradd -r -g rubyuser -G sudo -p "" --create-home rubyuser

ENV RBENV_ROOT=/usr/local/rbenv

# chown after running `rbenv init` because that command creates some subdirectories
RUN git clone https://github.com/rbenv/rbenv.git      ${RBENV_ROOT} && \
    git clone https://github.com/rbenv/ruby-build.git ${RBENV_ROOT}/plugins/ruby-build && \
    \
    echo "export RBENV_ROOT=/usr/local/rbenv" >> /etc/rubybashrc && \
    echo "export PATH=$RBENV_ROOT/bin:\$PATH" >> /etc/rubybashrc && \
    $RBENV_ROOT/bin/rbenv init - --no-rehash bash >> /etc/rubybashrc && \
    echo "source /etc/rubybashrc" >> /etc/bashrc && \
    echo "source /etc/rubybashrc" >> /etc/bash.bashrc && \
    \
    chown -R rubyuser:rubyuser ${RBENV_ROOT} && \
    find ${RBENV_ROOT} -type d -print0 | sudo xargs -0 chmod g+sw
ENV BASH_ENV=/etc/rubybashrc


##
##  set up rake-compiler and install bootstrap rubies
##
USER rubyuser

ENV RBENV_RUBIES="3.1.7 4.0.2"

# Install the bootstrap rubies
RUN bash -c " \
  echo 'gem: --no-ri --no-rdoc --no-document' >> ~/.gemrc && \
  export CFLAGS='-s -O3 -fno-fast-math -fPIC' && \
  for v in ${RBENV_RUBIES} ; do \
    rbenv install \$v -- --disable-install-doc ; \
  done && \
  find ${RBENV_ROOT} -type d -print0 | sudo xargs -0 chmod g+w && \
  find ${RBENV_ROOT} -path '*/plugins/*.rb' -print0 | sudo xargs -0 chmod g+w \
"

# Install rake-compiler and patch it to build and install static libraries for Linux rubies
COPY build/patches /home/rubyuser/patches
RUN bash -c " \
  for v in ${RBENV_RUBIES} ; do \
    rbenv shell \$v && \
    gem install rake-compiler -v1.3.1 && \
    cd ${RBENV_ROOT}/versions/\$v/lib/ruby/gems/*/gems/rake-compiler-1.3.1 && \
    for patch in /home/rubyuser/patches/rake-compiler-1.3.1/*.patch ; do \
      patch -p1 < \$patch ; \
    done \
  done \
"

# Install rake-compiler's cross rubies in global dir instead of /root
RUN sudo mkdir -p /usr/local/rake-compiler && \
    sudo chown rubyuser.rubyuser /usr/local/rake-compiler && \
    ln -s /usr/local/rake-compiler ~/.rake-compiler

USER root

##
## Install cross compilers
##
RUN apt-get -y update && \
    apt-get install -y  gcc-mingw-w64-i686 g++-mingw-w64-i686  && \
    rm -rf /var/lib/apt/lists/*


USER rubyuser

RUN bash -c " \
  rbenv shell 3.1.7 && \
  export CPPFLAGS='' && \
  export CFLAGS='-O1 -fno-omit-frame-pointer -fno-fast-math -fstack-protector-strong -s' && \
  export LDFLAGS='-pipe -s' && \
  export LIBS='-l:libssp.a -lpthread' && \
   \
  export MAKE='make V=1' && \
  rake-compiler cross-ruby VERSION=3.3.11:3.2.11:3.1.7:3.0.7 HOST=i686-w64-mingw32 && \
  rm -rf ~/.rake-compiler/builds ~/.rake-compiler/sources \
"
RUN bash -c " \
  rbenv shell 4.0.2 && \
  export CPPFLAGS='' && \
  export CFLAGS='-O1 -fno-omit-frame-pointer -fno-fast-math -fstack-protector-strong -s' && \
  export LDFLAGS='-pipe -s' && \
  export LIBS='-l:libssp.a -lpthread' && \
   \
  export MAKE='make V=1' && \
  rake-compiler cross-ruby VERSION=3.4.9 HOST=i686-w64-mingw32 && \
  rm -rf ~/.rake-compiler/builds ~/.rake-compiler/sources \
"
# "


# RubyInstaller doesn't install libgcc -> link it static.
RUN find /usr/local/rake-compiler/ruby/*mingw*/ -name rbconfig.rb | while read f ; do sed -i 's/."LDFLAGS". = "/&-static-libgcc /' $f ; done
# Don't link to static libruby
RUN find /usr/local/rake-compiler/ruby -name lib*-ruby*.dll.a | while read f ; do n=`echo $f | sed s/.dll//` ; mv $f $n ; done



##
##  Final adjustments
##
USER root

# Install wrappers for strip commands as a workaround for "Protocol error" in boot2docker.
COPY build/strip_wrapper_vbox /root/
RUN mv /usr/bin/i686-w64-mingw32-strip /usr/bin/i686-w64-mingw32-strip.bin && \
    ln /root/strip_wrapper_vbox /usr/bin/i686-w64-mingw32-strip

# Use posix pthread for mingw so that C++ standard library for thread could be
# available such as std::thread, std::mutex, so on.
# https://sourceware.org/pthreads-win32/
RUN printf "1\n" | update-alternatives --config i686-w64-mingw32-gcc && \
    printf "1\n" | update-alternatives --config i686-w64-mingw32-g++




# Install SIGINT forwarder
COPY build/sigfw.c /root/
RUN gcc $HOME/sigfw.c -o /usr/bin/sigfw

# Install user mapper
COPY build/runas /usr/bin/
COPY build/rcd-env.sh /etc/profile.d/
RUN echo 'source /etc/profile.d/rcd-env.sh' >> /etc/rubybashrc

# Install sudoers configuration
COPY build/sudoers /etc/sudoers.d/rake-compiler-dock

RUN bash -c "rbenv global 4.0.2"

ENV RUBY_CC_VERSION=4.0.2:3.4.9:3.3.11:3.2.11:3.1.7:3.0.7

CMD bash
