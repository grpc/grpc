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
COPY build/mk_musl_cross.sh /tmp
RUN /tmp/mk_musl_cross.sh x86_64-unknown-linux-musl


USER rubyuser

RUN bash -c " \
  rbenv shell 3.1.7 && \
  export CPPFLAGS='' && \
  export CFLAGS='-O1 -fno-omit-frame-pointer -fno-fast-math -fstack-protector-strong -s' && \
  export LDFLAGS='-pipe -s' && \
   \
   \
  export MAKE='make V=1' && \
  rake-compiler cross-ruby VERSION=3.3.11:3.2.11:3.1.7:3.0.7 HOST=x86_64-unknown-linux-musl && \
  rm -rf ~/.rake-compiler/builds ~/.rake-compiler/sources \
"
RUN bash -c " \
  rbenv shell 4.0.2 && \
  export CPPFLAGS='' && \
  export CFLAGS='-O1 -fno-omit-frame-pointer -fno-fast-math -fstack-protector-strong -s' && \
  export LDFLAGS='-pipe -s' && \
   \
   \
  export MAKE='make V=1' && \
  rake-compiler cross-ruby VERSION=4.0.2:3.4.9 HOST=x86_64-unknown-linux-musl && \
  rm -rf ~/.rake-compiler/builds ~/.rake-compiler/sources \
"
# "

# Avoid linking against libruby shared object.
# See also https://github.com/rake-compiler/rake-compiler-dock/issues/13
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name libruby.so | xargs rm
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name libruby-static.a | while read f ; do cp $f `echo $f | sed s/-static//` ; done
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name libruby.a | while read f ; do ar t $f | xargs ar d $f ; done
RUN find /usr/local/rake-compiler/ruby/*linux*/ -name mkmf.rb | while read f ; do sed -i ':a;N;$!ba;s/TRY_LINK = [^\n]*\n[^\n]*\n[^\n]*LOCAL_LIBS)/& -lruby-static -lpthread -lrt -ldl -latomic/' $f ; done




##
##  Final adjustments
##
USER root





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
