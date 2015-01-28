# Dockerfile for gRPC Go
FROM golang:1.4

# Install SSH to that Go source can be pulled securely.
RUN apt-get update && apt-get install -y ssh

# Install a GitHub SSH service credential that gives access to the GitHub repo while it's private
#
# TODO: remove this once the repo is public
ADD .ssh .ssh
RUN chmod 600 /.ssh/github.rsa
RUN mkdir -p $HOME/.ssh && echo 'Host github.com' > $HOME/.ssh/config
RUN echo "    IdentityFile /.ssh/github.rsa" >> $HOME/.ssh/config
RUN echo 'StrictHostKeyChecking no' >> $HOME/.ssh/config

# Force go get to use the GitHub ssh url instead of https, and use the SSH creds
RUN git config --global url."git@github.com:".insteadOf "https://github.com/"

# Get the source from GitHub
RUN go get github.com/google/grpc-go

# Build the interop client and server
RUN cd src/github.com/google/grpc-go/interop/client && go install
RUN cd src/github.com/google/grpc-go/interop/server && go install

# Specify the default command such that the interop server runs on its known testing port
CMD ["/bin/bash", "-c 'cd src/github.com/google/grpc-go/interop/server && go run server.go --use_tls=true --port=8020'"]
