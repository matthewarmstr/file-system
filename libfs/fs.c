#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define SB_PADDING_LEN 4079

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
struct superblock superblk;

int fs_mount(const char *diskname)
{
	/* TODO: Phase 1 */
	if (block_disk_open(diskname) == -1) {
		return -1;
	}

	void * buf;

	block_read(0, buf);
	// gets items for superblock from buf

	return 0;
}

int fs_umount(void)
{
	/* TODO: Phase 1 */
}

int fs_info(void)
{
	/* TODO: Phase 1 */
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

