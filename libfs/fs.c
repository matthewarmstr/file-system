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

struct FAT_block {
	int16_t entries[FB_ENTRIES_PER_BLOCK];
};

struct FAT_node {
	struct FAT_block* data;
	struct FAT_node* next;
};

struct FAT_section {
	struct FAT_node* start;
	struct FAT_node* end;
};

struct root_directory {
	int8_t filename[FS_FILENAME_LEN];
	int64_t file_size;
	int16_t first_data_block_index;
	int8_t padding[RD_PADDING_LEN];
};

struct superblock *superblk;
struct root_directory *root_dir;
struct FAT_section *FAT_nodes;

int fs_mount(const char *diskname)
{
	if (block_disk_open(diskname) == -1) {
		return -1;
	}

	// Store superblock info
	void * buf;
	int readret = block_read(0, buf);
	if (readret == -1) {
		fprintf(stderr, "Could not read from block (superblock)\n");
		return -1;
	}
	superblk = buf;

	// Check that signature is correct
	if (superblk->signature != SB_EXPECTED_SIG) {
		return -1;
	}
	
	// Check that superblock has correct number of blocks on disk
	int blkcount = block_disk_count();
	if (blkcount != superblk->num_blocks_on_disk) {
		return -1;
	}

	// Load FAT blocks
	for (int8_t i = 1; i <= superblk->num_blocks_FAT; i++) {		
		struct FAT_block* new_FAT_blk = (struct FAT_block*)malloc(sizeof(struct FAT_block));
		struct FAT_node* new_FAT_node = (struct FAT_node*)malloc(sizeof(struct FAT_node));
		if (new_FAT_blk == NULL || new_FAT_node == NULL) {
			fprintf(stderr, "Malloc failed");
			return -1;
		}
		new_FAT_node->data = new_FAT_blk;
		if (i == 1) {
			FAT_nodes->start = new_FAT_node;
			FAT_nodes->end = new_FAT_node;
		} else {
			// struct FAT_node* prev_FAT_node = FAT_nodes->end;
			FAT_nodes->end->next = new_FAT_node;
			FAT_nodes->end = new_FAT_node;
		}
		block_read(i, buf);
		FAT_nodes->end->data = buf;

		if (i == superblk->num_blocks_FAT) {
			FAT_nodes->end->next = NULL;
		}
	}

	// Store root directory info
	block_read(superblk->root_block_index, buf);
	root_dir = buf;

	return 0;
}

int fs_umount(void)
{
	// Check if no FS is mounted or if disk cannot be closed
	if (block_disk_count() == -1) {
		return -1;
	}

	for (int i = 0; i < superblk->num_data_blocks; i++) {
		void * buf_out;
	}
}

int fs_info(void)
{
	//Check if no virtual disk file is open
	if (block_disk_count() == -1) {
		return -1;
	}

	printf("FS Info:\n");
	printf("total_blk_count=%d\n", superblk->num_blocks_on_disk);
	printf("fat_blk_count=%d\n", superblk->num_blocks_FAT);
	printf("rdir_blk=%d\n", superblk->root_block_index);
	printf("data_blk=%d\n", superblk->data_block_start_index);
	printf("data_blk_count=%d\n", superblk->num_data_blocks);
	printf("fat_free_ratio=[unknown]\n"); // TO DO: FILL IN LATER
	printf("rdir_free_ratio=[unknown]\n"); // TO DO: FILL IN LATER
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

