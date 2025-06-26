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

"""Python version numbers and their encoding ("magic number")."""

import struct

# These constants are from Python-3.x.x/Lib/importlib/_bootstrap_external.py
PYTHON_MAGIC = {
    # Python 1
    20121: (1, 5),
    50428: (1, 6),
    # Python 2
    50823: (2, 0),
    60202: (2, 1),
    60717: (2, 2),
    62011: (2, 3),  # a0
    62021: (2, 3),  # a0
    62041: (2, 4),  # a0
    62051: (2, 4),  # a3
    62061: (2, 4),  # b1
    62071: (2, 5),  # a0
    62081: (2, 5),  # a0
    62091: (2, 5),  # a0
    62092: (2, 5),  # a0
    62101: (2, 5),  # b3
    62111: (2, 5),  # b3
    62121: (2, 5),  # c1
    62131: (2, 5),  # c2
    62151: (2, 6),  # a0
    62161: (2, 6),  # a1
    62171: (2, 7),  # a0
    62181: (2, 7),  # a0
    62191: (2, 7),  # a0
    62201: (2, 7),  # a0
    62211: (2, 7),  # a0
    # Python 3
    3000: (3, 0),
    3010: (3, 0),
    3020: (3, 0),
    3030: (3, 0),
    3040: (3, 0),
    3050: (3, 0),
    3060: (3, 0),
    3061: (3, 0),
    3071: (3, 0),
    3081: (3, 0),
    3091: (3, 0),
    3101: (3, 0),
    3103: (3, 0),
    3111: (3, 0),  # a4
    3131: (3, 0),  # a5
    # Python 3.1
    3141: (3, 1),  # a0
    3151: (3, 1),  # a0
    # Python 3.2
    3160: (3, 2),  # a0
    3170: (3, 2),  # a1
    3180: (3, 2),  # a2
    # Python 3.3
    3190: (3, 3),  # a0
    3200: (3, 3),  # a0
    3220: (3, 3),  # a1
    3230: (3, 3),  # a4
    # Python 3.4
    3250: (3, 4),  # a1
    3260: (3, 4),  # a1
    3270: (3, 4),  # a1
    3280: (3, 4),  # a1
    3290: (3, 4),  # a4
    3300: (3, 4),  # a4
    3310: (3, 4),  # rc2
    # Python 3.5
    3320: (3, 5),  # a0
    3330: (3, 5),  # b1
    3340: (3, 5),  # b2
    3350: (3, 5),  # b2
    3351: (3, 5),  # 3.5.2
    # Python 3.6
    3360: (3, 6),  # a0
    3361: (3, 6),  # a0
    3370: (3, 6),  # a1
    3371: (3, 6),  # a1
    3372: (3, 6),  # a1
    3373: (3, 6),  # b1
    3375: (3, 6),  # b1
    3376: (3, 6),  # b1
    3377: (3, 6),  # b1
    3378: (3, 6),  # b2
    3379: (3, 6),  # rc1
    # Python 3.7
    3390: (3, 7),  # a1
    3391: (3, 7),  # a2
    3392: (3, 7),  # a4
    3393: (3, 7),  # b1
    3394: (3, 7),  # b5
    # Python 3.8
    3400: (3, 8),  # a1
    3401: (3, 8),  # a1
    3410: (3, 8),  # a1
    3411: (3, 8),  # b2
    3412: (3, 8),  # b2
    3413: (3, 8),  # b4
    # Python 3.9
    3420: (3, 9),  # a0
    3421: (3, 9),  # a0
    3422: (3, 9),  # a0
    3423: (3, 9),  # a2
    3424: (3, 9),  # a2
    3425: (3, 9),  # a2
    # Python 3.10
    3430: (3, 10),  # a1
    3431: (3, 10),  # a1
    3432: (3, 10),  # a2
    3433: (3, 10),  # a2
    3434: (3, 10),  # a6
    3435: (3, 10),  # a7
    3436: (3, 10),  # b1
    3437: (3, 10),  # b1
    3438: (3, 10),  # b1
    3439: (3, 10),  # b1
    # Python 3.11
    3450: (3, 11),  # a1
    3451: (3, 11),  # a1
    3452: (3, 11),  # a1
    3453: (3, 11),  # a1
    3454: (3, 11),  # a1
    3455: (3, 11),  # a1
    3456: (3, 11),  # a1
    3457: (3, 11),  # a1
    3458: (3, 11),  # a1
    3459: (3, 11),  # a1
    3460: (3, 11),  # a1
    3461: (3, 11),  # a1
    3462: (3, 11),  # a2
    3463: (3, 11),  # a3
    3464: (3, 11),  # a3
    3465: (3, 11),  # a3
    3466: (3, 11),  # a4
    3467: (3, 11),  # a4
    3468: (3, 11),  # a4
    3469: (3, 11),  # a4
    3470: (3, 11),  # a4
    3471: (3, 11),  # a4
    3472: (3, 11),  # a4
    3473: (3, 11),  # a4
    3474: (3, 11),  # a4
    3475: (3, 11),  # a5
    3476: (3, 11),  # a5
    3477: (3, 11),  # a5
    3478: (3, 11),  # a5
    3479: (3, 11),  # a5
    3480: (3, 11),  # a5
    3481: (3, 11),  # a5
    3482: (3, 11),  # a5
    3483: (3, 11),  # a5
    3484: (3, 11),  # a5
    3485: (3, 11),  # a5
    3486: (3, 11),  # a6
    3487: (3, 11),  # a6
    3488: (3, 11),  # a6
    3489: (3, 11),  # a6
    3490: (3, 11),  # a6
    3491: (3, 11),  # a6
    3492: (3, 11),  # a7
    3493: (3, 11),  # a7
    3494: (3, 11),  # a7
    3495: (3, 11),  # b4
    # Python 3.12
    3500: (3, 12),  # a1
    3501: (3, 12),  # a1
    3502: (3, 12),  # a1
    3503: (3, 12),  # a1
    3504: (3, 12),  # a1
    3505: (3, 12),  # a1
    3506: (3, 12),  # a1
    3507: (3, 12),  # a1
    3508: (3, 12),  # a1
    3509: (3, 12),  # a1
    3510: (3, 12),  # a2
    3511: (3, 12),  # a2
    3512: (3, 12),  # a2
    3513: (3, 12),  # a4
    3514: (3, 12),  # a4
    3515: (3, 12),  # a5
    3516: (3, 12),  # a5
    3517: (3, 12),  # a5
    3518: (3, 12),  # a6
    3519: (3, 12),  # a6
    3520: (3, 12),  # a6
    3521: (3, 12),  # a7
    3522: (3, 12),  # a7
    3523: (3, 12),  # a7
    3524: (3, 12),  # a7
    3525: (3, 12),  # b1
    3526: (3, 12),  # b1
    3527: (3, 12),  # b1
    3528: (3, 12),  # b1
    3529: (3, 12),  # b1
    3530: (3, 12),  # b1
    3531: (3, 12),  # b1
}


def magic_number_to_version(magic_number):
    """Return the Python version belonging to the magic number in the pyc head.

    The magic number is encoded in the first two bytes of a .pyc file.
    It translates to a (major, minor) version. It never has a "micro" version,
    because Python bytecode encoding doesn't change between micro version.

    Arguments:
      magic_number: A 16 bit number, either as an integer or little-endian
      encoded as a string.

    Returns:
      A tuple (major, minor), e.g. (3, 7).
    """
    if not isinstance(magic_number, int):
        magic_number = struct.unpack("<H", magic_number)[0]
    return PYTHON_MAGIC[magic_number]
