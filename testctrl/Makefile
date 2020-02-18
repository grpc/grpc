build: deps
	go build

deps:
	go get -v -d ./...

proto:
	protoc -I ./proto proto/*.proto --go_out=plugins=grpc:./proto

test:
	go test ./...

.PHONY: build deps proto test

