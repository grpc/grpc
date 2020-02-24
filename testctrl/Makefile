all: deps proto testctrl driver
	go build -v ./...

clean:
	rm testctrl
	rm proto/*.pb.go

deps:
	go get -v -d ./...

driver:
	go build -v -o bin/driver ./cmd/driver

fmt:
	for file in $$(find **/*.go); do gofmt -w $$file; done

lint:
	golint ./...

proto:
	protoc -I ./proto proto/*.proto --go_out=plugins=grpc:./proto
	go build -v ./proto

rebuild: clean proto testctrl driver

svc:
	go build -v -o bin/svc ./cmd/svc

test:
	go test ./...

.PHONY: all clean deps driver fmt lint proto rebuild svc test
