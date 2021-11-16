# syntax=docker/dockerfile:1

FROM golang:1.16-alpine
WORKDIR /app

# Copy over source files
COPY *.go ./
COPY transfer/ ./transfer
COPY config/ ./config

# Download necessary go modules
COPY go.mod ./
COPY go.sum ./
RUN go mod download

RUN go build -o /database_transfer

EXPOSE 8080

CMD [ "/database_transfer" ]
