FROM fedora:28

RUN yum update -y && yum install \
    git \
    gcc \
    gcc-c++ \
    make \
    automake \
    libtool \
    which \
    findutils \
    python -y

RUN yum install zip -y

RUN python -m pip install six

# Use of fedora28 is intended to test the build with gcc 8.1
RUN gcc --version | head -n1 | grep 8.1
RUN g++ --version | head -n1 | grep 8.1

CMD ["bash"]
