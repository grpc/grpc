### 38290-backup-poller repro


#### 1.72: Current broken

```sh
python3.12 -m venv --upgrade-deps .venv
source ./.venv/bin/activate
pip install -r requirements.txt
python -m grpc_tools.protoc --proto_path=./protos --python_out=./protos/v6 --pyi_out=./protos/v6 --grpc_python_out=./protos/v6 ./protos/server.proto
```

#### 1.67: Last working

```sh
python3.12 -m venv --upgrade-deps .venv-167
source ./.venv-167/bin/activate
pip install -r requirements-167.txt
python -m grpc_tools.protoc --proto_path=./protos --python_out=./protos/v5 --pyi_out=./protos/v5 --grpc_python_out=./protos/v5 ./protos/server.proto
```

#### Repro (any venv)

First terminal:

```sh
python client.py
```

Second terminal:

```sh
python server.py
```

Press Ctrl+C once to restart the server, twice for full exit.

