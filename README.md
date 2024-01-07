# FAT-Based File System
A simple yet robust file system built using a File Allocation Table (FAT) written in C. This file system supports up to 128 files in a single root directory. Rigorous testing methods incorporating multiple Unix shell scripts and sample text files to verify the functionality of the file system. 

This custom file system operates on a virtual disk that appears as a single binary file. A specific block API manipulates the data stored directly on the virtual disk, which includes actions to open and close the disk and read/write entire blocks of data. The file system is then tasked with using the block API to manage how data inside files is added, edited, and removed from the disk. This higher abstraction layer can mount/unmount a virtual disk and read/write as many data blocks as needed for the file system to maintain the disk's contents as expected.

The virtual disk contains the following blocks (similar to sectors on a real hard drive):
- Superblock
- File Allocation Table
- Root Directory
- Data Blocks (includes the contents of all stored files)

Each of these blocks of the virtual disk is 4096 bytes. Virtual disks can be created anywhere from 1 to 8192 data blocks.

*Note: each FAT entry is 16 bits, or 2 bytes, wide - this will be discussed further in the 'FAT' section below*

## Superblock
The first block on the disk. It contains general information about the contents of the disk. Its structure includes:
| Offset | Length (in bytes) | Description                              |
|--------|-------------------|------------------------------------------|
| 0x00   | 8                 | Signature                                |
| 0x08   | 2                 | Total number of blocks allocated on disk |
| 0x0A   | 2                 | Index of block for root directory        |
| 0x0C   | 2                 | Index of first data block                |
| 0x0E   | 2                 | Number of reserved data blocks           |
| 0x10   | 1                 | Number of blocks reserved for FAT        |
| 0x11   | 4079              | Unused/Padding                           |

Since the signature is required to have a length of 8 bytes, the variable representing the signature was given a type of *int64_t*, which stores an unsigned integer with a width of exactly 64 bits (64 / 8 = 8 bytes). Likewise, the variables representing the total number of allocated blocks, the index of the block for the root directory, the index of the first data block, and the number of reserved data blocks were given types of *int16_t* (an unsigned integer with a width of exactly 2 bytes, or 16 bits). Finally, a maximum of 4 blocks could be reserved for the FAT (8192 data blocks * 2 byte-wide entries / 4096 bytes per block), so the variable representing this statistic was given a type of *int8_t* (integer value of 4 can be stored in a byte).

For example, if a file system of 5000 data blocks is created, the size of the FAT would be 5000 * 2, or 10000 bytes. This means that the FAT would require 3 reserved blocks (10000 bytes / 4096 bytes per block = 2.44 blocks, thus requiring 3 blocks). As a result, the superblock occupies the first block (index 0), followed by the FAT taking up three blocks (indexes 1, 2, and 3). The index for the root directory block would have a block index of 4, followed by the index of the starting data block having a block index of 5. This file system would contain a total of 5005 blocks, with 5000 blocks being reserved for data, 1 for the signature, 3 for the FAT entries, and 1 more for the root directory block.

## Fat Allocation Table (FAT)
This file system utilizes the FAT16 format, meaning that each entry within the FAT is 16 bits wide. The first entry within the FAT always contains the *FAT_EOC* "End-of-Chain" value (0xFFFF). FAT entries corresponding to empty data blocks are marked with 0. To indicate the end of a file stored in the disk's data blocks, the same *FAT_EOC* is inserted at the appropriate FAT location representing the data block with the last file data.

This table illustrates an example of a FAT that contains three files:
| FAT Index |    0   | 1 | 2 | 3 |    4   | 5 | 6 | 7 |    8   |  9 | 10 |   11   | 12 | ... |
|-----------|--------|---|---|---|--------|---|---|---|--------|----|----|--------|----|-----|
|  Contents | 0xFFFF | 2 | 3 | 4 | 0xFFFF | 0 | 8 | 9 | 0xFFFF | 10 | 11 | 0xFFFF |  0 | ... |

This FAT example represents 3 files contained in the disk's data blocks, which correspond to the FAT index in the table. The locations of each file's data can be accessed by traversing the FAT table:
1. The first file is contained within consecutive data blocks (D.B. 1, 2, 3, 4, and 5), which span 4 data blocks. This file is ... bytes large.
2. ...

## Root Directory
