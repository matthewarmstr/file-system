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

struct __attribute__ ((__packed__)) FAT_node {
	uint16_t entries[FB_ENTRIES_PER_BLOCK];
	struct FAT_node* next;
};

struct __attribute__ ((__packed__)) FAT_section {
	struct FAT_node* start;
	struct FAT_node* end;
};

struct __attribute__ ((__packed__)) root_directory {
	int8_t *filename;
	int64_t file_size;
	int16_t first_data_block_index;
	int8_t padding[RD_PADDING_LEN];
};

struct superblock superblk;
struct FAT_section FAT_nodes;
struct __attribute__ ((__packed__)) root_directory rootdir_arr[FS_FILE_MAX_COUNT];
int FS_mounted = 0;

int fs_mount(const char *diskname)
{
	// Check if virtual disk cannot be opened or if no valid file system can be located
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

	FS_mounted = 1;
	return 0;
}

int fs_umount(void)
{
	// Check if no FS is mounted, if disk cannot be closed, or if there are still open file descriptors
	if (!FS_mounted || block_disk_count() == -1) {
		return -1;
		// TO DO: add to error check
	}

	// Write out FAT blocks to disk
	int writeret;
	struct FAT_node* curr = FAT_nodes.start;
	for (int8_t i = 1; i <= superblk.num_blocks_FAT; i++) {		
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
	// Check if no virtual disk is open
	if (block_disk_count() == -1) {
		return -1;
	}

	printf("FS Info:\n");
	printf("total_blk_count=%d\n", superblk.num_blocks_on_disk);
	printf("fat_blk_count=%d\n", superblk.num_blocks_FAT);
	printf("rdir_blk=%d\n", superblk.root_block_index);
	printf("data_blk=%d\n", superblk.data_block_start_index);
	printf("data_blk_count=%d\n", superblk.num_data_blocks);

	// Count number of free FAT blocks
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
	
	// Count number of empty filenames in root directory
	int num_rdir_free = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (&rootdir_arr[i].filename[0] == NULL) {
			num_rdir_free++;
		}
	}
	printf("rdir_free_ratio=%d/128\n", num_rdir_free);
	return 0;
}

int fs_create(const char *filename)
{
	// Count number of non-empty filenames in root directory
	int num_rdir_files = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (&rootdir_arr[i].filename[0] != NULL) {
			num_rdir_files++;
		}
		// Check if filename to insert already exists in root directory
		if (!(strcmp((char*)&rootdir_arr[i].filename, filename))) { 
			return -1;
		}
	}
	
	// Check if no FS mounted, if filename is invalid, if if given filename is too long, 
	// or if root directory alrady has the max # of files
	if (!FS_mounted || &filename[0] == NULL || strlen(filename) >= FS_FILENAME_LEN 
		|| num_rdir_files >= FS_FILE_MAX_COUNT) {
		return -2;
	}

	// Locate empty entry in root directory
	int empty_entry_idx;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (&rootdir_arr[i].filename[0] == NULL) {
			empty_entry_idx = i;
			break;
		}
	}

	// Create new & empty file with given filename at empty entry in root directory
	strcpy((char*)&rootdir_arr[empty_entry_idx].filename, filename);
	rootdir_arr[empty_entry_idx].file_size = 0;

	
	int first_free_FAT_idx;
	int free_FAT_found = 0;
	struct FAT_node* curr = FAT_nodes.start;
	for (int i = 0; i < superblk.num_blocks_FAT; i++) {
		for (int j = 0; j < FB_ENTRIES_PER_BLOCK; j++) {
			if (curr->entries[j] == 0) {
				first_free_FAT_idx = j;
				free_FAT_found = 1;
				break;
			}
		}
		if (free_FAT_found == 1) {
			break;
		}
		curr = curr->next;
	}
	curr->entries[first_free_FAT_idx] = FAT_EOC;
	rootdir_arr[empty_entry_idx].first_data_block_index = first_free_FAT_idx;
	
	return 0;
}

int fs_delete(const char *filename)
{
	int filename_exists = 0;
	int filename_rootdir_inx;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		// Check if filename to delete already exists in root directory
		if (!(strcmp((char*)&rootdir_arr[i].filename, filename))) { 
			filename_exists = 1;
			filename_rootdir_inx = i;
			rootdir_arr[i].filename = NULL;
			break;
		}
	}
	
	// Check if no FS is mounted, if filename is invalid, if filename does not exist in root directory,
	// or if filename is currently opened
	if (!FS_mounted || &filename[0] == NULL || strlen(filename) >= FS_FILENAME_LEN || !filename_exists) {
		return -1;
	}

	// Calculate FAT block entry of index to delete from 
	int FAT_block_num = rootdir_arr[filename_rootdir_inx].first_data_block_index / FB_ENTRIES_PER_BLOCK;
	int delete_FAT_inx = rootdir_arr[filename_rootdir_inx].first_data_block_index % FB_ENTRIES_PER_BLOCK;
	
	// Locate appropriate FAT node
	struct FAT_node* curr = FAT_nodes.start;
	for (int i = 0; i < superblk.num_blocks_FAT; i++) {
		if (i == FAT_block_num) {
			break;
		}
		curr = curr->next;
	}

	// Go through entries of FAT block to delete from
	int delete_FAT_next_inx;
	while(1) {
		delete_FAT_next_inx = curr->entries[delete_FAT_inx];
		curr->entries[delete_FAT_inx] = 0;
		if (delete_FAT_next_inx == FAT_EOC) {
			break;
		}
		delete_FAT_next_inx = delete_FAT_next_inx % FB_ENTRIES_PER_BLOCK;
		if (delete_FAT_inx > delete_FAT_next_inx) {
			// Assuming in-order FAT entries
			curr = curr->next;
		}
		delete_FAT_inx = delete_FAT_next_inx;
	}
	return 0;
}

int fs_ls(void)
{
	// Check if no FS is mounted
	if (!FS_mounted) {
		return -1;
	}

	// Iterate through root directory and files with their names and sizes
	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (&rootdir_arr[i].filename[0] != NULL) {
			char filename[FS_FILENAME_LEN];
			memcpy(filename, (void*)&rootdir_arr[i].filename, FS_FILENAME_LEN);
			printf("file: %s, ", filename);
			printf("size: %ld, ", rootdir_arr[i].file_size);
			printf("data_blk: %d\n", rootdir_arr[i].first_data_block_index);
		}
	}
	return 0;
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

