from _typeshed import Incomplete
from typing import Any

class ExtendedOperationContainer:
    def __init__(self, connection) -> None: ...

class StandardExtendedOperations(ExtendedOperationContainer):
    def who_am_i(self, controls: Incomplete | None = None): ...
    def modify_password(
        self,
        user: Incomplete | None = None,
        old_password: Incomplete | None = None,
        new_password: Incomplete | None = None,
        hash_algorithm: Incomplete | None = None,
        salt: Incomplete | None = None,
        controls: Incomplete | None = None,
    ): ...
    def paged_search(
        self,
        search_base,
        search_filter,
        search_scope="SUBTREE",
        dereference_aliases="ALWAYS",
        attributes: Incomplete | None = None,
        size_limit: int = 0,
        time_limit: int = 0,
        types_only: bool = False,
        get_operational_attributes: bool = False,
        controls: Incomplete | None = None,
        paged_size: int = 100,
        paged_criticality: bool = False,
        generator: bool = True,
    ): ...
    def persistent_search(
        self,
        search_base: str = "",
        search_filter: str = "(objectclass=*)",
        search_scope="SUBTREE",
        dereference_aliases="NEVER",
        attributes="*",
        size_limit: int = 0,
        time_limit: int = 0,
        controls: Incomplete | None = None,
        changes_only: bool = True,
        show_additions: bool = True,
        show_deletions: bool = True,
        show_modifications: bool = True,
        show_dn_modifications: bool = True,
        notifications: bool = True,
        streaming: bool = True,
        callback: Incomplete | None = None,
    ): ...
    def funnel_search(
        self,
        search_base: str = "",
        search_filter: str = "",
        search_scope="SUBTREE",
        dereference_aliases="NEVER",
        attributes="*",
        size_limit: int = 0,
        time_limit: int = 0,
        controls: Incomplete | None = None,
        streaming: bool = False,
        callback: Incomplete | None = None,
    ): ...

class NovellExtendedOperations(ExtendedOperationContainer):
    def get_bind_dn(self, controls: Incomplete | None = None): ...
    def get_universal_password(self, user, controls: Incomplete | None = None): ...
    def set_universal_password(self, user, new_password: Incomplete | None = None, controls: Incomplete | None = None): ...
    def list_replicas(self, server_dn, controls: Incomplete | None = None): ...
    def partition_entry_count(self, partition_dn, controls: Incomplete | None = None): ...
    def replica_info(self, server_dn, partition_dn, controls: Incomplete | None = None): ...
    def start_transaction(self, controls: Incomplete | None = None): ...
    def end_transaction(self, commit: bool = True, controls: Incomplete | None = None): ...
    def add_members_to_groups(self, members, groups, fix: bool = True, transaction: bool = True): ...
    def remove_members_from_groups(self, members, groups, fix: bool = True, transaction: bool = True): ...
    def check_groups_memberships(self, members, groups, fix: bool = False, transaction: bool = True): ...

class MicrosoftExtendedOperations(ExtendedOperationContainer):
    def dir_sync(
        self,
        sync_base,
        sync_filter: str = "(objectclass=*)",
        attributes="*",
        cookie: Incomplete | None = None,
        object_security: bool = False,
        ancestors_first: bool = True,
        public_data_only: bool = False,
        incremental_values: bool = True,
        max_length: int = 2147483647,
        hex_guid: bool = False,
    ): ...
    def modify_password(self, user, new_password, old_password: Incomplete | None = None, controls: Incomplete | None = None): ...
    def unlock_account(self, user): ...
    def add_members_to_groups(self, members, groups, fix: bool = True): ...
    def remove_members_from_groups(self, members, groups, fix: bool = True): ...
    def persistent_search(
        self,
        search_base: str = "",
        search_scope="SUBTREE",
        attributes="*",
        streaming: bool = True,
        callback: Incomplete | None = None,
    ): ...

class ExtendedOperationsRoot(ExtendedOperationContainer):
    standard: Any
    novell: Any
    microsoft: Any
    def __init__(self, connection) -> None: ...
