FROM fedora:28

RUN yum update -y && yum install git -y
RUN yum update -y && yum install gcc gcc-c++ -y
RUN yum install make automake -y
RUN git clone https://github.com/grpc/grpc.git --recursive
RUN yum install which -y
RUN yum install findutils -y
#RUN cd grpc && make interop_client -j8
CMD ["bash"]

