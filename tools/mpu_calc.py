# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from collections import namedtuple
from bitarray import bitarray
import struct

MIN_REGION_SIZE = 32
MAX_REGION_SIZE = 4 * 1024 * 1024 * 1024  # 4 GB
NUM_SUBREGIONS = 8

# Check if we're building for ARMv8 MPU (no subregions)
# This will be set by the wscript before calling find_subregions_for_region
ARMV8_MPU = False

def set_armv8_mode(is_armv8):
    """Set whether to use ARMv8 MPU mode (no subregions)"""
    global ARMV8_MPU
    ARMV8_MPU = is_armv8


def round_up_to_power_of_two(x):
    """ Find the next power of two that is eqaul to or greater than x

    >>> round_up_to_power_of_two(4)
    4
    >>> round_up_to_power_of_two(5)
    8
    """

    return 2**((x-1).bit_length())


MpuRegion = namedtuple('MpuRegion', ['address', 'size', 'disabled_subregion'])


def find_subregions_for_region(address, size):
    """ Find a MPU region configuration that will exactly match the provided combination of
        address and size.

        ARMv8 MPU: Regions must be 32-byte aligned and sized, with no subregion support.
        ARMv7 MPU: Supports regions that are power of 2 sized and aligned to their size,
        with 8 subregions that can be individually enabled/disabled.

    >>> find_subregions_for_region(0x0, 512)
    MpuRegion(address=0, size=512, disabled_subregion=0)
    >>> find_subregions_for_region(0x0, 513)
    Traceback (most recent call last):
        ...
    Exception: No solution found

    For example, the snowy layout
    Result is a 256kb region at 0x20000000 with a region disabled mask of 0b11000111
    >>> find_subregions_for_region(0x20018000, 96 * 1024)
    MpuRegion(address=536870912, size=262144, disabled_subregion=199)
    """

    if ARMV8_MPU:
        # ARMv8 MPU: Must be exactly 32-byte aligned, no subregions
        if address % 32 != 0:
            raise Exception(f"ARMv8 MPU requires 32-byte aligned address, got 0x{address:x}")
        if size % 32 != 0:
            raise Exception(f"ARMv8 MPU requires 32-byte aligned size, got 0x{size:x}")
        if size < MIN_REGION_SIZE or size > MAX_REGION_SIZE:
            raise Exception(f"ARMv8 MPU size out of range: 0x{size:x}")
        
        # Return region with no subregion mask (0 = all enabled)
        return MpuRegion(address, size, 0)

    # ARMv7 MPU: Find the range of sizes to attempt to match against. Anything smaller than the size of the
    # region itself wont work, and anything where a single subregion is too big won't work either.
    smallest_block_size = max(round_up_to_power_of_two(size), MIN_REGION_SIZE)
    largest_block_size = min(round_up_to_power_of_two(size * NUM_SUBREGIONS), MAX_REGION_SIZE)

    # Iterate over the potentional candidates from smallest to largest
    current_block_size = smallest_block_size
    while current_block_size <= largest_block_size:
        subregion_size = current_block_size // NUM_SUBREGIONS

        start_in_block = address % current_block_size
        end_in_block = start_in_block + size

        if (start_in_block % subregion_size == 0 and
                end_in_block % subregion_size == 0 and
                end_in_block <= current_block_size):

            # This region fits in the provided region and both the start and end are aligned with
            # subregion boundries. This will work!

            block_start_addresss = address - start_in_block

            start_enabled_subregion = start_in_block // subregion_size
            end_enabled_subregion = end_in_block // subregion_size

            disabled_subregions = bitarray(8, endian='little')
            disabled_subregions.setall(True)
            disabled_subregions[start_enabled_subregion:end_enabled_subregion] = False

            disabled_subregions_bytes = disabled_subregions.tobytes()
            disabled_subregions_int, = struct.unpack('B', disabled_subregions_bytes)

            return MpuRegion(block_start_addresss, current_block_size, disabled_subregions_int)

        current_block_size *= 2
    else:
        raise Exception("No solution found")

if __name__ == '__main__':
    import doctest
    doctest.testmod()
