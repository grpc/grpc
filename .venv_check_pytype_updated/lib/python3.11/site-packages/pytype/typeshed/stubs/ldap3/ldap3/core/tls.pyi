from _typeshed import Incomplete
from typing import Any

use_ssl_context: bool

class Tls:
    ssl_options: Any
    validate: Any
    ca_certs_file: Any
    ca_certs_path: Any
    ca_certs_data: Any
    private_key_password: Any
    version: Any
    private_key_file: Any
    certificate_file: Any
    valid_names: Any
    ciphers: Any
    sni: Any
    def __init__(
        self,
        local_private_key_file: Incomplete | None = None,
        local_certificate_file: Incomplete | None = None,
        validate=...,
        version: Incomplete | None = None,
        ssl_options: Incomplete | None = None,
        ca_certs_file: Incomplete | None = None,
        valid_names: Incomplete | None = None,
        ca_certs_path: Incomplete | None = None,
        ca_certs_data: Incomplete | None = None,
        local_private_key_password: Incomplete | None = None,
        ciphers: Incomplete | None = None,
        sni: Incomplete | None = None,
    ) -> None: ...
    def wrap_socket(self, connection, do_handshake: bool = False) -> None: ...
    def start_tls(self, connection): ...

def check_hostname(sock, server_name, additional_names) -> None: ...
