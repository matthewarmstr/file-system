#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define SB_PADDING_LEN 4079
#define SB_EXPECTED_SIG 6000536558536704837
#define FB_ENTRIES_PER_BLOCK 2048
#define RD_PADDING_LEN 10
#define FAT_EOC 0xFFFF

struct superblock {
	int64_t signature;
	int16_t num_blocks_on_disk;
	int16_t root_block_index;
	int16_t data_block_start_index;
	int16_t num_data_blocks;
	int8_t num_blocks_FAT;
	int8_t padding[SB_PADDING_LEN];
};

struct FAT_node {
	uint16_t entries[FB_ENTRIES_PER_BLOCK];
	struct FAT_node* next;
};

struct __attribute__ ((__packed__)) FAT_section {
	struct FAT_node* start;
	struct FAT_node* end;
};

struct __attribute__ ((__packed__)) root_directory {
	int8_t filename[FS_FILENAME_LEN];
	int64_t file_size;
	int16_t first_data_block_index;
	int8_t padding[RD_PADDING_LEN];
};

struct superblock superblk;
struct FAT_section FAT_nodes;
struct __attribute__ ((__packed__)) root_directory* rootdir_arr[FS_FILE_MAX_COUNT];

int fs_mount(const char *diskname)
{
	// Check if diskname is invalid or if virtual disk cannot be opened
	if (block_disk_open(diskname) == -1) {
		return -1;
	}

	// Store superblock info
	int readret = block_read(0, &superblk);
	if (readret == -1) {
		fprintf(stderr, "Could not read from disk (superblock)\n");
		return -1;
	}

	// Check that signature is correct
	if (superblk.signature != SB_EXPECTED_SIG) {
		return -1;
	}
	
	// Check that superblock has correct number of blocks on disk
	int blkcount = block_disk_count();
	if (blkcount != superblk.num_blocks_on_disk) {
		return -1;
	}

	// Load FAT blocks
	for (int8_t i = 1; i <= superblk.num_blocks_FAT; i++) {		
		struct FAT_node* new_FAT_node = (struct FAT_node*)malloc(sizeof(struct FAT_node));
		if (new_FAT_node == NULL) {
			fprintf(stderr, "Malloc failed");
			return -1;
		}
		readret = block_read(i, new_FAT_node->entries);
		if (readret == -1) {
			fprintf(stderr, "Could not read from disk (FAT block)\n");
			return -1;
		}
		if (i == 1) {
			// Setup new FAT node structure
			new_FAT_node->next = NULL;
			FAT_nodes.start = new_FAT_node;
			FAT_nodes.end = new_FAT_node;
		} else {
			// Add to FAT node structure
			FAT_nodes.end->next = new_FAT_node;
			FAT_nodes.end = new_FAT_node;
		}

		if (i == superblk.num_blocks_FAT) {
			FAT_nodes.end->next = NULL;
		}
	}

	// Store root directory info
	readret = block_read(superblk.root_block_index, &rootdir_arr);
	if (readret == -1) {
		fprintf(stderr, "Could not read from disk (root directory)\n");
		return -1;
	}

	return 0;
}

int fs_umount(void)
{
	// Check if no FS is mounted or if disk cannot be closed
	if (block_disk_count() == -1) {
		return -1;
	}

	// Write out FAT blocks to disk
	int writeret;
	for (int8_t i = 1; i <= superblk.num_blocks_FAT; i++) {		
		struct FAT_node* curr = FAT_nodes.start;
		writeret = block_write(i, curr->entries);
		if (writeret == -1) {
			fprintf(stderr, "Could not write to disk (FAT block)\n");
			return -1;
		}
		curr = curr->next;
	}

	// Write out root directory to disk
	writeret = block_write(superblk.root_block_index, &rootdir_arr);
	if (writeret == -1) {
		fprintf(stderr, "Could not write to disk (root directory)\n");
		return -1;
	}

	// Free the allocated data for FAT nodes
	struct FAT_node* curr;
	for (int8_t i = 0; i < superblk.num_blocks_FAT; i++) {
		curr = FAT_nodes.start;
		FAT_nodes.start = FAT_nodes.start->next;
		free(curr);
	}
	
	// Close the currently open virtual disk
	if (block_disk_close() == -1) {
		return -1;
	}
	return 0;
}

int fs_info(void) {
	//Check if no virtual disk file is open
	if (block_disk_count() == -1) {
		return -1;
	}

	printf("FS Info:\n");
	printf("total_blk_count=%d\n", superblk.num_blocks_on_disk);
	printf("fat_blk_count=%d\n", superblk.num_blocks_FAT);
	printf("rdir_blk=%d\n", superblk.root_block_index);
	printf("data_blk=%d\n", superblk.data_block_start_index);
	printf("data_blk_count=%d\n", superblk.num_data_blocks);

	// Check for free FAT blocks
	int num_FAT_blks = 0;
	struct FAT_node* curr = FAT_nodes.start;
	for (int i = 0; i < superblk.num_blocks_FAT; i++) {
		for (int j = 0; j < FB_ENTRIES_PER_BLOCK; j++) {
			if (curr->entries[j] == 0) {
				num_FAT_blks++;
			}
		}
		curr = curr->next;
	}
	printf("fat_free_ratio=%d/%d\n", num_FAT_blks, superblk.num_data_blocks);
	
	// Check for empty filenames in root directory
	int num_rdir_free = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (&rootdir_arr[i]->filename[0] == NULL) {
			num_rdir_free++;
		}
	}
	printf("rdir_free_ratio=%d/128\n", num_rdir_free);
	return 0;
}

int fs_create(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_ls(void)
{
	/* TODO: Phase 2 */
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */
}

int fs_close(int fd)
{
	/* TODO: Phase 3 */
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

