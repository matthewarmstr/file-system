# File System Based on FAT16
A simple yet robust file system built using a File Allocation Table (FAT) written in C. This file system supports up to 128 files in a single root directory. Rigorous testing methods incorporating multiple Unix shell scripts and sample text files were used to continually test the functionality of the file system. A custom makefile was also constructed to compile the necessary code for the disk and filesystem layers.

## Interacting with the File System
To begin, navigate to the `apps` directory and run the makefile with the following commands:
~~~
cd apps
make
~~~
This will compile the C code that runs the file system and automatically create a virtual disk named 'disk.fs' with 8192 data blocks. To create one with a different number of data blocks (greater than 0, less than 8193), type the following command:
~~~
./fs_make.x <diskname> <data block count (1-8192)>
~~~
Next, the executable for `test_fs.c` can be used in combination with any text file (many are included, and more can be added) to interact with the file system. The available input commands for using the file system in the terminal include:
~~~
./test_fs.x info <diskname>              | "Display info about the file system, including its total block count, the number of data blocks,
                                            the number of FAT blocks, the number of data blocks, the block index numbers of
                                            the root directory and first data block, the ratio of free data blocks to the number of FAT blocks,
                                            and number of stored files out of 128"
./test_fs.x ls <diskname>                | "List names of files stored on the root directory of disk"
./test_fs.x add <diskname> <filename>    | "Add a file to disk"
./test_fs.x rm <diskname> <filename>     | "Remove a file from disk"
./test_fs.x cat <diskname> <filename>    | "View the contents of a file stored on disk"
./test_fs.x stat <diskname> <filename>   | "Get the size of a file stored on disk in bytes"
~~~

To verify the functionality of the file system, a shell script was constructed in `apps/tester_grade.sh` to continuously check the output of the file system. It combines various terminal commands with existing text files, along with some script files in the `apps/scripts` directory, to execute many different disk storage scenarios that the file system needs to handle. To see how these individual script files work, see the instructions at `apps/scripts/SCRIPTS.md`. The entire shell script can be executed by running `./tester_grade.sh` while in the `apps` directory.

## Overview of File System Structure
This custom file system operates on a virtual disk that appears as a single binary file. A specific block API manipulates the data stored directly on the virtual disk, which includes actions to open and close the disk and read/write entire blocks of data. The file system is then tasked with using the block API to manage how data inside files is added, edited, and removed from the disk. This higher abstraction layer can mount/unmount a virtual disk and read/write as many data blocks as needed for the file system to maintain the disk's contents as expected.

The virtual disk contains the following blocks (similar to sectors on a real hard drive):
- Superblock
- File Allocation Table
- Root Directory
- Data Blocks (includes the contents of all stored files)

Each of these blocks of the virtual disk is 4096 bytes. Virtual disks can be created anywhere from 1 to 8192 data blocks.

*Note: each FAT entry is 16 bits, or 2 bytes, wide - this will be covered further in the FAT discussion section below*

### Superblock
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

### Fat Allocation Table (FAT)
Since this file system utilizes the FAT16 format, each entry within the FAT is 16 bits wide. The first entry within the FAT always contains the *FAT_EOC* "End-of-Chain" value (0xFFFF). FAT entries corresponding to empty data blocks are marked with 0. To indicate the end of a file stored in the disk's data blocks, the same *FAT_EOC* is inserted at the appropriate FAT location representing the data block with the last file data.

This table illustrates an example of a FAT that contains three files:
| FAT Index |    0   | 1 | 2 | 3 |    4   | 5 | 6 | 7 |    8   |  9 | 10 |   11   | 12 | ... |
|-----------|--------|---|---|---|--------|---|---|---|--------|----|----|--------|----|-----|
|  Contents | 0xFFFF | 2 | 3 | 4 | 0xFFFF | 0 | 8 | 9 | 0xFFFF | 10 | 11 | 0xFFFF |  0 | ... |

This FAT example represents 3 files contained in the disk's data blocks, which correspond to the FAT index in the table. The locations of each file's data can be accessed by traversing the FAT table:
1. The first file is contained within 4 consecutive data blocks (D.B. 1, 2, 3, 4), indicating that the size of this file is between 12288 and 16384 bytes. 
2. The second file is contained within 2 non-consecutive data blocks (D.B. 6, 8), indicating that the size of this file is between 4096 and 8192 bytes.
3. The third file is contained within 4 consecutive data blocks (D.B. 7, 9, 10, 11), indicating that the size of this file is between 12288 and 16384 bytes.

If a new file is written to the disk with the FAT shown above and requires two data blocks, the contents of FAT entry 5 would become 12, and entry 12 would contain *0xFFFF*.

### Root Directory
This contains the information necessary to locate the maximum of 128 files stored within the virtual disk. It is simply an array of 128 entries, with each entry being 32 bytes wide according to the following format:
| Offset | Length (in bytes) | Description               |
|--------|-------------------|---------------------------|
| 0x00   | 16                | Filename                  |
| 0x10   | 4                 | File size (in bytes)      |
| 0x14   | 2                 | Index of first data block |
| 0x16   | 10                | Unused/Padding            |

The index of the first data block would correspond with the index value in the FAT (and hence the data block) that each file starts at (with the above example, 1 would be the starting index of the first file, 6 for the second file, and 7 for the second).
