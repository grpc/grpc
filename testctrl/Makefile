all: deps proto testctrl driver
	go build -v ./...

clean:
	rm -fr bin
	rm -fr genproto/*

deps:
	go get -v -d ./...

driver:
	go build -v -o bin/driver ./cmd/driver

fmt:
	for file in $$(find **/*.go); do gofmt -w $$file; done

lint:
	golint ./...

proto:
	rm -fr genproto/*
	mkdir -p ../tmp/src/proto
	cp -r ../src/proto/grpc ../tmp/src/proto

	for protofile in $$(ls ../tmp/src/proto/grpc/**/*.proto); do \
		echo "Adding option go_package to $$protofile" && \
		scripts/fix_proto.py fix $$protofile; \
	done

	for protofile in $$(ls ../tmp/src/proto/grpc/core/*.proto); do \
		echo "Generating go code for $$protofile" && \
		protoc -I ../tmp \
					 $$protofile \
					 --go_out=plugins=grpc:./genproto; \
	done

	for protofile in $$(ls ../tmp/src/proto/grpc/testing/*.proto); do \
		echo "Generating go code for $$protofile" && \
		protoc -I ../tmp \
					 $$protofile \
					 --go_out=plugins=grpc:./genproto; \
	done

	for protofile in $$(ls ./proto/*.proto); do \
		echo $$protofile && \
		protoc -I ../third_party/googleapis \
					 -I ../tmp \
					 -I ./proto \
					 $$protofile \
					 --go_out=plugins=grpc:./genproto; \
	done

	rm -fr ../tmp/
	mv genproto/github.com/grpc/grpc/testctrl/genproto/grpc genproto/
	mv genproto/github.com/grpc/grpc/testctrl/genproto/testctrl genproto/
	rm -fr genproto/github.com

	echo "Cherry-picking the generated go to conflicting/unnecessary files"
	rm genproto/grpc/testing/compiler_test.pb.go
	rm genproto/grpc/testing/echo.pb.go
	rm genproto/grpc/testing/echo_messages.pb.go
	rm genproto/grpc/testing/empty_service.pb.go
	rm genproto/grpc/testing/metrics.pb.go
	rm genproto/grpc/testing/proxy-service.pb.go
	rm genproto/grpc/testing/simple_messages.pb.go

rebuild: clean proto testctrl driver

svc:
	go build -v -o bin/svc ./cmd/svc

test:
	go test ./...

.PHONY: all clean deps driver fmt lint proto rebuild svc test
