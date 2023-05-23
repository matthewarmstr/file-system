#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define SB_PADDING_LEN 4079
#define SB_EXPECTED_SIG 6000536558536704837
#define RD_PADDING_LEN 80

/* TODO: Phase 1 */
struct superblock {
	int64_t signature;
	int16_t num_blocks_on_disk;
	int16_t root_block_index;
	int16_t data_block_start_index;
	int16_t num_data_blocks;
	int8_t num_blocks_FAT;
	int8_t padding[SB_PADDING_LEN];
};

struct root_dir {
	int128_t filename;
	int64_t file_size;
	int16_t first_data_block_index;
	int8_t padding[RD_PADDING_LEN]
}

struct superblock *superblk;

int fs_mount(const char *diskname)
{
	if (block_disk_open(diskname) == -1) {
		return -1;
	}

	// Store superblock info
	void * buf;
	block_read(0, buf);
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

