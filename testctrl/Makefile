all: deps proto testctrl driver
	go build -v ./...

clean:
	rm -r bin
	rm -r proto/*.pb.go

deps:
	go get -v -d ./...

driver:
	go build -v -o bin/driver ./cmd/driver

fmt:
	for file in $$(find **/*.go); do gofmt -w $$file; done

lint:
	golint ./...

proto:
	mkdir -p genproto/github.com/grpc/grpc

	for protofile in $$(ls ../third_party/googleapis/google/rpc/*.proto); do \
		echo $$protofile && \
		protoc -I ../third_party/googleapis $$protofile --go_out=./genproto; \
	done

	for protofile in $$(ls ../third_party/googleapis/google/longrunning/*.proto); do \
		echo $$protofile && \
		protoc -I ../third_party/googleapis \
					 $$protofile \
					 --go_out=plugins=grpc:./genproto; \
	done

	for protofile in $$(ls ../src/proto/grpc/testing/*.proto); do \
		echo $$protofile && \
		protoc -I ../ \
					 $$protofile \
					 --go_out=plugins=grpc:./genproto/github.com/grpc/grpc; \
	done

	for protofile in $$(ls ./proto/*.proto); do \
		echo $$protofile && \
		protoc -I ../third_party/googleapis \
					 -I ../ \
					 -I ./proto \
					 $$protofile \
					 --go_out=plugins=grpc:./genproto; \
	done

rebuild: clean proto testctrl driver

svc:
	go build -v -o bin/svc ./cmd/svc

test:
	go test ./...

.PHONY: all clean deps driver fmt lint proto rebuild svc test
