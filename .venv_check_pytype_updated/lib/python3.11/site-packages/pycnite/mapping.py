# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Mapping of opcode codes to names."""

from typing import Dict, Mapping, Optional, Tuple

OpMap = Dict[int, str]
Overlay = Dict[int, Optional[str]]
OpArgs = Mapping[str, int]
OpCaches = Mapping[str, int]


def _overlay_mapping(mapping: OpMap, new_entries: Overlay) -> OpMap:
    ret: Overlay = mapping.copy()
    ret.update(new_entries)
    return {k: v for k, v in ret.items() if v is not None}


PYTHON_3_8_MAPPING: OpMap = {
    1: "POP_TOP",
    2: "ROT_TWO",
    3: "ROT_THREE",
    4: "DUP_TOP",
    5: "DUP_TOP_TWO",
    6: "ROT_FOUR",  # ROT_FOUR returns under a different, cooler id!
    9: "NOP",
    10: "UNARY_POSITIVE",
    11: "UNARY_NEGATIVE",
    12: "UNARY_NOT",
    15: "UNARY_INVERT",
    16: "BINARY_MATRIX_MULTIPLY",
    17: "INPLACE_MATRIX_MULTIPLY",
    19: "BINARY_POWER",
    20: "BINARY_MULTIPLY",
    22: "BINARY_MODULO",
    23: "BINARY_ADD",
    24: "BINARY_SUBTRACT",
    25: "BINARY_SUBSCR",
    26: "BINARY_FLOOR_DIVIDE",
    27: "BINARY_TRUE_DIVIDE",
    28: "INPLACE_FLOOR_DIVIDE",
    29: "INPLACE_TRUE_DIVIDE",
    50: "GET_AITER",
    51: "GET_ANEXT",
    52: "BEFORE_ASYNC_WITH",
    53: "BEGIN_FINALLY",
    54: "END_ASYNC_FOR",
    55: "INPLACE_ADD",
    56: "INPLACE_SUBTRACT",
    57: "INPLACE_MULTIPLY",
    59: "INPLACE_MODULO",
    60: "STORE_SUBSCR",
    61: "DELETE_SUBSCR",
    62: "BINARY_LSHIFT",
    63: "BINARY_RSHIFT",
    64: "BINARY_AND",
    65: "BINARY_XOR",
    66: "BINARY_OR",
    67: "INPLACE_POWER",
    68: "GET_ITER",
    69: "GET_YIELD_FROM_ITER",
    70: "PRINT_EXPR",
    71: "LOAD_BUILD_CLASS",
    72: "YIELD_FROM",
    73: "GET_AWAITABLE",
    75: "INPLACE_LSHIFT",
    76: "INPLACE_RSHIFT",
    77: "INPLACE_AND",
    78: "INPLACE_XOR",
    79: "INPLACE_OR",
    81: "WITH_CLEANUP_START",
    82: "WITH_CLEANUP_FINISH",
    83: "RETURN_VALUE",
    84: "IMPORT_STAR",
    85: "SETUP_ANNOTATIONS",
    86: "YIELD_VALUE",
    87: "POP_BLOCK",
    88: "END_FINALLY",
    89: "POP_EXCEPT",
    90: "STORE_NAME",
    91: "DELETE_NAME",
    92: "UNPACK_SEQUENCE",
    93: "FOR_ITER",
    94: "UNPACK_EX",
    95: "STORE_ATTR",
    96: "DELETE_ATTR",
    97: "STORE_GLOBAL",
    98: "DELETE_GLOBAL",
    100: "LOAD_CONST",
    101: "LOAD_NAME",
    102: "BUILD_TUPLE",
    103: "BUILD_LIST",
    104: "BUILD_SET",
    105: "BUILD_MAP",
    106: "LOAD_ATTR",
    107: "COMPARE_OP",
    108: "IMPORT_NAME",
    109: "IMPORT_FROM",
    110: "JUMP_FORWARD",
    111: "JUMP_IF_FALSE_OR_POP",
    112: "JUMP_IF_TRUE_OR_POP",
    113: "JUMP_ABSOLUTE",
    114: "POP_JUMP_IF_FALSE",
    115: "POP_JUMP_IF_TRUE",
    116: "LOAD_GLOBAL",
    122: "SETUP_FINALLY",
    124: "LOAD_FAST",
    125: "STORE_FAST",
    126: "DELETE_FAST",
    130: "RAISE_VARARGS",
    131: "CALL_FUNCTION",
    132: "MAKE_FUNCTION",
    133: "BUILD_SLICE",
    134: "MAKE_CLOSURE",
    135: "LOAD_CLOSURE",
    136: "LOAD_DEREF",
    137: "STORE_DEREF",
    138: "DELETE_DEREF",
    141: "CALL_FUNCTION_KW",
    142: "CALL_FUNCTION_EX",
    143: "SETUP_WITH",
    144: "EXTENDED_ARG",
    145: "LIST_APPEND",
    146: "SET_ADD",
    147: "MAP_ADD",
    148: "LOAD_CLASSDEREF",
    149: "BUILD_LIST_UNPACK",
    150: "BUILD_MAP_UNPACK",
    151: "BUILD_MAP_UNPACK_WITH_CALL",
    152: "BUILD_TUPLE_UNPACK",
    153: "BUILD_SET_UNPACK",
    154: "SETUP_ASYNC_WITH",
    155: "FORMAT_VALUE",
    156: "BUILD_CONST_KEY_MAP",
    157: "BUILD_STRING",
    158: "BUILD_TUPLE_UNPACK_WITH_CALL",
    160: "LOAD_METHOD",
    161: "CALL_METHOD",
    162: "CALL_FINALLY",
    163: "POP_FINALLY",
}

PYTHON_3_9_MAPPING: OpMap = _overlay_mapping(
    PYTHON_3_8_MAPPING,
    {
        48: "RERAISE",
        49: "WITH_EXCEPT_START",
        53: None,  # was BEGIN_FINALLY in 3.8
        74: "LOAD_ASSERTION_ERROR",
        81: None,  # was WITH_CLEANUP_START in 3.8
        82: "LIST_TO_TUPLE",  # was WITH_CLEANUP_FINISH in 3.8
        88: None,  # was END_FINALLY in 3.8
        117: "IS_OP",
        118: "CONTAINS_OP",
        121: "JUMP_IF_NOT_EXC_MATCH",
        149: None,  # was BUILD_LIST_UNPACK in 3.8
        150: None,  # was BUILD_MAP_UNPACK in 3.8
        151: None,  # was BUILD_MAP_UNPACK_WITH_CALL in 3.8
        152: None,  # was BUILD_TUPLE_UNPACK in 3.8
        153: None,  # was BUILD_SET_UNPACK in 3.8
        158: None,  # was BUILD_TUPLE_UNPACK_WITH_CALL in 3.8
        162: "LIST_EXTEND",  # was CALL_FINALLY in 3.8
        163: "SET_UPDATE",  # was POP_FINALLY in 3.8
        164: "DICT_MERGE",
        165: "DICT_UPDATE",
    },
)

PYTHON_3_10_MAPPING: OpMap = _overlay_mapping(
    PYTHON_3_9_MAPPING,
    {
        30: "GET_LEN",
        31: "MATCH_MAPPING",
        32: "MATCH_SEQUENCE",
        33: "MATCH_KEYS",
        34: "COPY_DICT_WITHOUT_KEYS",
        48: None,  # was RERAISE in 3.9
        99: "ROT_N",
        119: "RERAISE",
        129: "GEN_START",
        152: "MATCH_CLASS",
    },
)

PYTHON_3_11_MAPPING: OpMap = _overlay_mapping(
    PYTHON_3_10_MAPPING,
    {
        0: "CACHE",
        2: "PUSH_NULL",  # was ROT_TWO in 3.10
        3: None,  # was ROT_THREE in 3.10
        4: None,  # was DUP_TOP in 3.10
        5: None,  # was DUP_TOP_TWO in 3.10
        6: None,  # was ROT_FOUR in 3.10
        16: None,  # was BINARY_MATRIX_MULTIPLY in 3.10
        17: None,  # was INPLACE_MATRIX_MULTIPLY in 3.10
        19: None,  # was BINARY_POWER in 3.10
        20: None,  # was BINARY_MULTIPLY in 3.10
        22: None,  # was BINARY_MODULO in 3.10
        23: None,  # was BINARY_ADD in 3.10
        24: None,  # was BINARY_SUBTRACT in 3.10
        26: None,  # was BINARY_FLOOR_DIVIDE in 3.10
        27: None,  # was BINARY_TRUE_DIVIDE in 3.10
        28: None,  # was INPLACE_FLOOR_DIVIDE in 3.10
        29: None,  # was INPLACE_TRUE_DIVIDE in 3.10
        34: None,  # was COPY_DICT_WITHOUT_KEYS in 3.10
        35: "PUSH_EXC_INFO",
        36: "CHECK_EXC_MATCH",
        37: "CHECK_EG_MATCH",
        53: "BEFORE_WITH",
        55: None,  # was INPLACE_ADD in 3.10
        56: None,  # was INPLACE_SUBTRACT in 3.10
        57: None,  # was INPLACE_MULTIPLY in 3.10
        59: None,  # was INPLACE_MODULO in 3.10
        62: None,  # was BINARY_LSHIFT in 3.10
        63: None,  # was BINARY_RSHIFT in 3.10
        64: None,  # was BINARY_AND in 3.10
        65: None,  # was BINARY_XOR in 3.10
        66: None,  # was BINARY_OR in 3.10
        67: None,  # was INPLACE_POWER in 3.10
        72: None,  # was YIELD_FROM in 3.10
        73: None,  # was GET_AWAITABLE in 3.10
        75: "RETURN_GENERATOR",  # was INPLACE_LSHIFT in 3.10
        76: None,  # was INPLACE_RSHIFT in 3.10
        77: None,  # was INPLACE_AND in 3.10
        78: None,  # was INPLACE_XOR in 3.10
        79: None,  # was INPLACE_OR in 3.10
        87: "ASYNC_GEN_WRAP",  # was POP_BLOCK in 3.10
        88: "PREP_RERAISE_STAR",
        99: "SWAP",  # was ROT_N in 3.10
        113: None,  # was JUMP_ABSOLUTE in 3.10
        114: "POP_JUMP_FORWARD_IF_FALSE",  # was POP_JUMP_IF_FALSE in 3.10
        115: "POP_JUMP_FORWARD_IF_TRUE",  # was POP_JUMP_IF_TRUE in 3.10
        120: "COPY",
        121: None,  # was JUMP_IF_NOT_EXC_MATCH in 3.10
        122: "BINARY_OP",  # was SETUP_FINALLY in 3.10
        123: "SEND",
        128: "POP_JUMP_FORWARD_IF_NOT_NONE",
        129: "POP_JUMP_FORWARD_IF_NONE",  # was GEN_START in 3.10
        131: "GET_AWAITABLE",  # was CALL_FUNCTION in 3.10
        134: "JUMP_BACKWARD_NO_INTERRUPT",
        135: "MAKE_CELL",  # was LOAD_CLOSURE in 3.10
        136: "LOAD_CLOSURE",  # was LOAD_DEREF in 3.10
        137: "LOAD_DEREF",  # was STORE_DEREF in 3.10
        138: "STORE_DEREF",  # was DELETE_DEREF in 3.10
        139: "DELETE_DEREF",
        140: "JUMP_BACKWARD",
        141: None,  # was CALL_FUNCTION_KW in 3.10
        143: None,  # was SETUP_WITH in 3.10
        149: "COPY_FREE_VARS",
        151: "RESUME",
        154: None,  # was SETUP_ASYNC_WITH in 3.10
        161: None,  # was CALL_METHOD in 3.10
        166: "PRECALL",
        171: "CALL",
        172: "KW_NAMES",
        173: "POP_JUMP_BACKWARD_IF_NOT_NONE",
        174: "POP_JUMP_BACKWARD_IF_NONE",
        175: "POP_JUMP_BACKWARD_IF_FALSE",
        176: "POP_JUMP_BACKWARD_IF_TRUE",
    },
)

PYTHON_3_12_MAPPING: OpMap = _overlay_mapping(
    PYTHON_3_11_MAPPING,
    {
        3: "INTERPRETER_EXIT",
        4: "END_FOR",
        5: "END_SEND",
        10: None,  # was UNARY_POSITIVE in 3.11
        17: "RESERVED",
        26: "BINARY_SLICE",
        27: "STORE_SLICE",
        55: "CLEANUP_THROW",
        70: None,  # was PRINT_EXPR in 3.11
        82: None,  # was LIST_TO_TUPLE in 3.11
        84: None,  # was IMPORT_STAR in 3.11
        86: None,  # was YIELD_VALUE in 3.11
        87: "LOAD_LOCALS",  # was ASYNC_GEN_WRAP in 3.11
        88: None,  # was PREP_RERAISE_STAR in 3.11
        111: None,  # was JUMP_IF_FALSE_OR_POP in 3.11
        112: None,  # was JUMP_IF_TRUE_OR_POP in 3.11
        114: "POP_JUMP_IF_FALSE",  # was POP_JUMP_FORWARD_IF_FALSE in 3.11
        115: "POP_JUMP_IF_TRUE",  # was POP_JUMP_FORWARD_IF_TRUE in 3.11
        121: "RETURN_CONST",
        127: "LOAD_FAST_CHECK",
        128: "POP_JUMP_IF_NOT_NONE",  # was POP_JUMP_FORWARD_IF_NOT_NONE in 3.11
        129: "POP_JUMP_IF_NONE",  # was POP_JUMP_FORWARD_IF_NONE in 3.11
        141: "LOAD_SUPER_ATTR",
        143: "LOAD_FAST_AND_CLEAR",
        148: None,  # was LOAD_CLASSDEREF in 3.11
        150: "YIELD_VALUE",
        160: None,  # was LOAD_METHOD in 3.11
        166: None,  # was PRECALL in 3.11
        173: "CALL_INTRINSIC_1",  # was POP_JUMP_BACKWARD_IF_NOT_NONE in 3.11
        174: "CALL_INTRINSIC_2",  # was POP_JUMP_BACKWARD_IF_NONE in 3.11
        175: (
            "LOAD_FROM_DICT_OR_GLOBALS"
        ),  # was POP_JUMP_BACKWARD_IF_FALSE in 3.11
        176: "LOAD_FROM_DICT_OR_DEREF",  # was POP_JUMP_BACKWARD_IF_TRUE in 3.11
    },
)


def get_mapping(version: Tuple[int, int]) -> OpMap:
    return {
        (3, 8): PYTHON_3_8_MAPPING,
        (3, 9): PYTHON_3_9_MAPPING,
        (3, 10): PYTHON_3_10_MAPPING,
        (3, 11): PYTHON_3_11_MAPPING,
        (3, 12): PYTHON_3_12_MAPPING,
    }[version]


# ----------------------------------------------------------
# Opcode argument types

CONST = 1  # references the constant table
NAME = 2  # references the name table
JREL = 4  # relative jump
JABS = 8  # absolute jump
LOCAL = 16  # references the varnames table
FREE = 32  # references "free variable" cells
NARGS = 64  # stores number of args + kwargs


PYTHON_3_8_ARG_TYPES: OpArgs = {
    "STORE_NAME": NAME,
    "DELETE_NAME": NAME,
    "FOR_ITER": JREL,
    "STORE_ATTR": NAME,
    "DELETE_ATTR": NAME,
    "STORE_GLOBAL": NAME,
    "DELETE_GLOBAL": NAME,
    "LOAD_CONST": CONST,
    "LOAD_NAME": NAME,
    "LOAD_ATTR": NAME,
    "IMPORT_NAME": NAME,
    "IMPORT_FROM": NAME,
    "JUMP_FORWARD": JREL,
    "JUMP_IF_FALSE_OR_POP": JABS,
    "JUMP_IF_TRUE_OR_POP": JABS,
    "JUMP_ABSOLUTE": JABS,
    "POP_JUMP_IF_FALSE": JABS,
    "POP_JUMP_IF_TRUE": JABS,
    "LOAD_GLOBAL": NAME,
    "CONTINUE_LOOP": JABS,
    "SETUP_LOOP": JREL,
    "SETUP_EXCEPT": JREL,
    "SETUP_FINALLY": JREL,
    "LOAD_FAST": LOCAL,
    "STORE_FAST": LOCAL,
    "DELETE_FAST": LOCAL,
    "STORE_ANNOTATION": NAME,
    "CALL_FUNCTION": NARGS,
    "LOAD_CLOSURE": FREE,
    "LOAD_DEREF": FREE,
    "STORE_DEREF": FREE,
    "DELETE_DEREF": FREE,
    "CALL_FUNCTION_VAR": NARGS,
    "CALL_FUNCTION_KW": NARGS,
    "CALL_FUNCTION_VAR_KW": NARGS,
    "SETUP_WITH": JREL,
    "LOAD_CLASSDEREF": FREE,
    "SETUP_ASYNC_WITH": JREL,
    "LOAD_METHOD": NAME,
    "CALL_METHOD": NARGS,
    "CALL_FINALLY": JREL,
    "JUMP_IF_NOT_EXC_MATCH": JABS,
    "POP_JUMP_FORWARD_IF_FALSE": JREL,
    "POP_JUMP_FORWARD_IF_TRUE": JREL,
    "SEND": JREL,
    "POP_JUMP_FORWARD_IF_NOT_NONE": JREL,
    "POP_JUMP_FORWARD_IF_NONE": JREL,
    "JUMP_BACKWARD_NO_INTERRUPT": JREL,
    "MAKE_CELL": FREE,
    "JUMP_BACKWARD": JREL,
    "KW_NAMES": CONST,
    "POP_JUMP_BACKWARD_IF_NOT_NONE": JREL,
    "POP_JUMP_BACKWARD_IF_NONE": JREL,
    "POP_JUMP_BACKWARD_IF_FALSE": JREL,
    "POP_JUMP_BACKWARD_IF_TRUE": JREL,
}

PYTHON_3_11_ARG_TYPES: OpArgs = {
    **PYTHON_3_8_ARG_TYPES,
    "JUMP_IF_FALSE_OR_POP": JREL,
    "JUMP_IF_TRUE_OR_POP": JREL,
}

PYTHON_3_12_ARG_TYPES: OpArgs = {
    **PYTHON_3_11_ARG_TYPES,
    "POP_JUMP_IF_FALSE": JREL,
    "POP_JUMP_IF_TRUE": JREL,
    "RETURN_CONST": CONST,
    "LOAD_FAST_CHECK": LOCAL,
    "POP_JUMP_IF_NOT_NONE": JREL,
    "POP_JUMP_IF_NONE": JREL,
    "LOAD_SUPER_ATTR": NAME,
    "LOAD_FAST_AND_CLEAR": LOCAL,
    "LOAD_FROM_DICT_OR_GLOBALS": NAME,
    "LOAD_FROM_DICT_OR_DEREF": FREE,
}


def arg_type(name: str, version: Tuple[int, int]) -> Optional[int]:
    if version >= (3, 12):
        argmap = PYTHON_3_12_ARG_TYPES
    elif version >= (3, 11):
        argmap = PYTHON_3_11_ARG_TYPES
    else:
        argmap = PYTHON_3_8_ARG_TYPES
    return argmap.get(name)


# ----------------------------------------------------------
# Function names referenced by CALL_INTRINSIC_{1,2} opcodes

PYTHON_3_12_INTRINSIC_1_DESCS = [
    "INTRINSIC_1_INVALID",
    "INTRINSIC_PRINT",
    "INTRINSIC_IMPORT_STAR",
    "INTRINSIC_STOPITERATION_ERROR",
    "INTRINSIC_ASYNC_GEN_WRAP",
    "INTRINSIC_UNARY_POSITIVE",
    "INTRINSIC_LIST_TO_TUPLE",
    "INTRINSIC_TYPEVAR",
    "INTRINSIC_PARAMSPEC",
    "INTRINSIC_TYPEVARTUPLE",
    "INTRINSIC_SUBSCRIPT_GENERIC",
    "INTRINSIC_TYPEALIAS",
]

PYTHON_3_12_INTRINSIC_2_DESCS = [
    "INTRINSIC_2_INVALID",
    "INTRINSIC_PREP_RERAISE_STAR",
    "INTRINSIC_TYPEVAR_WITH_BOUND",
    "INTRINSIC_TYPEVAR_WITH_CONSTRAINTS",
    "INTRINSIC_SET_FUNCTION_TYPE_PARAMS",
]


def intrinsic_1_desc(arg: int, version: Tuple[int, int]) -> Optional[str]:
    if version >= (3, 12):
        descs = PYTHON_3_12_INTRINSIC_1_DESCS
    else:
        descs = []
    return descs[arg]


def intrinsic_2_desc(arg: int, version: Tuple[int, int]) -> Optional[str]:
    if version >= (3, 12):
        descs = PYTHON_3_12_INTRINSIC_2_DESCS
    else:
        descs = []
    return descs[arg]


# ----------------------------------------------------------
# Inline cache entry counts

PYTHON_3_11_CACHES: OpCaches = {
    "BINARY_OP": 1,
    "BINARY_SUBSCR": 4,
    "CALL": 4,
    "COMPARE_OP": 2,
    "LOAD_ATTR": 4,
    "LOAD_GLOBAL": 5,
    "LOAD_METHOD": 10,
    "PRECALL": 1,
    "STORE_ATTR": 4,
    "STORE_SUBSCR": 1,
    "UNPACK_SEQUENCE": 1,
}

PYTHON_3_12_CACHES: OpCaches = {
    **PYTHON_3_11_CACHES,
    "BINARY_SUBSCR": 1,
    "CALL": 3,
    "COMPARE_OP": 1,
    "FOR_ITER": 1,
    "LOAD_ATTR": 9,
    "LOAD_GLOBAL": 4,
    "LOAD_METHOD": 0,
    "LOAD_SUPER_ATTR": 1,
    "SEND": 1,
}


def caches(name: str, version: Tuple[int, int]) -> int:
    if version >= (3, 12):
        cachesmap = PYTHON_3_12_CACHES
    elif version >= (3, 11):
        cachesmap = PYTHON_3_11_CACHES
    else:
        cachesmap = {}
    return cachesmap.get(name, 0)
