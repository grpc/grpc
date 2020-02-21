build: deps proto
	go build ./...

clean:
	rm testctrl
	rm proto/*.pb.go

fmt:
	for file in $$(find **/*.go); do gofmt -w $$file; done

deps:
	go get -v -d ./...

lint:
	golint ./...

proto:
	protoc -I ./proto proto/*.proto --go_out=plugins=grpc:./proto

test:
	go test ./...

.PHONY: build clean deps proto test

