### 38290-backup-poller repro


#### 1.72: Current broken

```sh
python3.12 -m venv --upgrade-deps .venv
source ./.venv/bin/activate
pip install -r requirements.txt
```

#### 1.67: Last working

```sh
python3.12 -m venv --upgrade-deps .venv-167
source ./.venv-167/bin/activate
pip install -r requirements-167.txt
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

