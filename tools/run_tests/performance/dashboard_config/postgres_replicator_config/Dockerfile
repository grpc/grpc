# syntax=docker/dockerfile:1

FROM golang:1.16-alpine
WORKDIR /app

# Copy config
COPY replicator_config.yaml ./
COPY test-infra ./test-infra

RUN cd test-infra \
    && go mod download \
    && go build -o /postgres_replicator cmd/postgres_replicator/main.go
RUN rm -rf test-infra


EXPOSE 8080
CMD [ "/postgres_replicator", "-c", "replicator_config.yaml" ]
