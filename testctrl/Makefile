build: deps
	go build

clean:
	rm testctrl
	rm proto/*.pb.go

deps:
	go get -v -d ./...

proto:
	protoc -I ./proto proto/*.proto --go_out=plugins=grpc:./proto

test:
	go test ./...

.PHONY: build clean deps proto test

