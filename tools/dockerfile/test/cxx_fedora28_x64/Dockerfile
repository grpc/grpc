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
RUN python -m pip install six
CMD ["bash"]

