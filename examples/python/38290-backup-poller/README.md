### 38290-backup-poller repro

#### master

```sh
# from repo root
GRPC_PYTHON_BUILD_WITH_SYSTEMD=1 GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip wheel . -w dist

cd examples/python/38290-backup-poller

python3.12 -m venv --upgrade-deps .venv-174
source ./.venv-174/bin/activate
pip install -r requirements-174.txt
python -m grpc_tools.protoc --proto_path=./protos --python_out=./protos/v6 --pyi_out=./protos/v6 --grpc_python_out=./protos/v6 ./protos/server.proto
```

#### 1.72: Current broken

```sh
python3.12 -m venv --upgrade-deps .venv-172
source ./.venv-172/bin/activate
pip install -r requirements-172.txt
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

