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

struct __attribute__ ((__packed__)) superblock {
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
	int8_t filename[FS_FILENAME_LEN];
	uint32_t file_size;
	uint16_t first_data_block_index;
	int8_t padding[RD_PADDING_LEN];
};

struct fd_entry {
	int used;
	int root_dir_index;
	size_t offset;
};

struct superblock superblk;
struct FAT_section FAT_nodes;
struct root_directory rootdir_arr[FS_FILE_MAX_COUNT];
struct fd_entry fd_table[FS_OPEN_MAX_COUNT];
int FS_mounted = 0;

size_t return_data_block (int fd) {
	int root_dir_idx = fd_table[fd].root_dir_index;
	//how many data blocks past the first one 
	int num_iterations = fd_table[fd].offset / BLOCK_SIZE;
	size_t curr_data_blk_idx = rootdir_arr[root_dir_idx].first_data_block_index % FB_ENTRIES_PER_BLOCK;
	size_t next_data_blk_idx;
		
	// Locate appropriate FAT node
	int FAT_block_num = rootdir_arr[root_dir_idx].first_data_block_index / FB_ENTRIES_PER_BLOCK;
	struct FAT_node* curr = FAT_nodes.start;
	for (int i = 0; i < superblk.num_blocks_FAT; i++) {
		if (i == FAT_block_num) {
			break;
		}
		curr = curr->next;
	}

	// Go through entries of FAT block to find data blk index
	for (int i = 0 ; i < num_iterations ; ++i) {
		next_data_blk_idx = curr->entries[curr_data_blk_idx];
		if (next_data_blk_idx == FAT_EOC) {
			break;
		}
		next_data_blk_idx = next_data_blk_idx % FB_ENTRIES_PER_BLOCK;
		if (curr_data_blk_idx > next_data_blk_idx) {
			// Assuming in-order FAT entries
			curr = curr->next;
		}
		curr_data_blk_idx = next_data_blk_idx;
	}
	// printf("DATA BLOCK TO READ: %ld\n", curr_data_blk_idx);
	return curr_data_blk_idx;
}

// returns -1 if there are no more open FAT entries
// otherwise, returns index of the new block allocated for fd.
size_t allocate_new_data_block (int fd, int current_last_data_blk) {
	size_t curr_fat_blk_idx = 1; 
	size_t next_fat_blk_idx;
		
	// Locate appropriate FAT node
	int FAT_block_num_last_entry = current_last_data_blk / FB_ENTRIES_PER_BLOCK;
	struct FAT_node* curr = FAT_nodes.start;
	
	//Go through entries of FAT block to find next empty entry
	for (int i = 0 ; i <  superblk.num_data_blocks ; ++i) {
		next_fat_blk_idx = curr->entries[curr_fat_blk_idx % FB_ENTRIES_PER_BLOCK];
		if ((next_fat_blk_idx != 0) && (i == superblk.num_data_blocks -1)) {
			return -1;
		}
		else if (next_fat_blk_idx == 0) {
			curr->entries[curr_fat_blk_idx % FB_ENTRIES_PER_BLOCK] = FAT_EOC;
			break;
		}

		if (curr_fat_blk_idx > next_fat_blk_idx % FB_ENTRIES_PER_BLOCK) {
			// Assuming in-order FAT entries
			curr = curr->next;
		}
		curr_fat_blk_idx = next_fat_blk_idx;
	}

	curr = FAT_nodes.start;
	for (int i = 0; i < FAT_block_num_last_entry; i++) {
		if (i == FAT_block_num_last_entry) {
			break;
		}
		curr = curr->next;
	}
	curr->entries[current_last_data_blk % FB_ENTRIES_PER_BLOCK] = curr_fat_blk_idx;
	return curr_fat_blk_idx;
}

size_t get_next_data_blk (int fd, int current_data_blk) {
		
	// Locate appropriate FAT node
	int FAT_block_num_last_entry = current_data_blk / FB_ENTRIES_PER_BLOCK;
	struct FAT_node* curr = FAT_nodes.start;
	for (int i = 0; i <= FAT_block_num_last_entry ; i++) {
		if (i == FAT_block_num_last_entry) {
			break;
		}
		curr = curr->next;
	}
	return curr->entries[current_data_blk % FB_ENTRIES_PER_BLOCK];
}

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

	//initialize fd table
	for (int i = 0 ; i < FS_OPEN_MAX_COUNT ; ++i) {
		fd_table[i].used = 0;
		fd_table[i].offset = 0;
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
	FS_mounted = 0;
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

	// Count number of free FAT entries
	int num_FAT_blks = 0;
	int num_data_blocks = superblk.num_data_blocks;
	int num_data_blocks_left = superblk.num_data_blocks;
	struct FAT_node* curr = FAT_nodes.start;
	for (int i = 0; i < superblk.num_blocks_FAT; i++) {
		// Make sure we are only checking the # of data block entries in FAT table
		if (num_data_blocks_left > FB_ENTRIES_PER_BLOCK) {
			num_data_blocks_left -= FB_ENTRIES_PER_BLOCK;
			num_data_blocks = FB_ENTRIES_PER_BLOCK;
		} else {
			num_data_blocks = num_data_blocks_left;
		}
		
		for (int j = 0; j < num_data_blocks ; j++) {
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
		if (rootdir_arr[i].filename[0] == '\0') {
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
		if (rootdir_arr[i].filename[0] != '\0') {
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
		return -1;
	}

	// Locate empty entry in root directory
	int empty_entry_idx;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (rootdir_arr[i].filename[0] == '\0') {
			empty_entry_idx = i;
			break;
		}
	}

	// Allocate space to store new filename string (2 bytes)
	int8_t *new_filename;
	new_filename = (int8_t*)strndup(filename, FS_FILENAME_LEN);
	
	// Create new & empty file with given filename at empty entry in root directory
	memcpy((int8_t*)&rootdir_arr[empty_entry_idx].filename, new_filename, FS_FILENAME_LEN);
	rootdir_arr[empty_entry_idx].file_size = 0;
	rootdir_arr[empty_entry_idx].first_data_block_index = FAT_EOC;
	
	// DELETE LATER - FOR REFERENCE ONLY
	// int first_free_FAT_idx;
	// int free_FAT_found = 0;
	// struct FAT_node* curr = FAT_nodes.start;
	// for (int i = 0; i < superblk.num_blocks_FAT; i++) {
	// 	for (int j = 1; j < FB_ENTRIES_PER_BLOCK; j++) {
	// 		if (curr->entries[j] == 0) {
	// 			// first_free_FAT_idx = j;
	// 			curr->entries[j] = FAT_EOC;
	// 			free_FAT_found = 1;
	// 			break;
	// 		}
	// 	}
	// 	if (free_FAT_found == 1) {
	// 		break;
	// 	}
	// 	curr = curr->next;
	// }

	// curr->entries[first_free_FAT_idx] = FAT_EOC;
	// rootdir_arr[empty_entry_idx].first_data_block_index = first_free_FAT_idx;
	
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
			rootdir_arr[i].filename[0] = '\0';
			break;
		}
	}
	
	// Check if no FS is mounted, if filename is invalid, if filename does not exist in root directory,
	// or if filename is currently opened
	if (!FS_mounted || &filename[0] == NULL || strlen(filename) >= FS_FILENAME_LEN || !filename_exists) {
		return -1;
	}

	// For stored files that are not empty, calculate FAT block entry of index to delete from
	if (rootdir_arr[filename_rootdir_inx].first_data_block_index != FAT_EOC) {
		int FAT_block_num = rootdir_arr[filename_rootdir_inx].first_data_block_index / FB_ENTRIES_PER_BLOCK;
		int delete_FAT_inx = rootdir_arr[filename_rootdir_inx].first_data_block_index % FB_ENTRIES_PER_BLOCK;
		printf("FAT_block_num: %d, delete_FAT_inx: %d\n", FAT_block_num, delete_FAT_inx);
		
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
		if (rootdir_arr[i].filename[0] != '\0') {
			char filename[FS_FILENAME_LEN];
			memcpy(filename, (void*)&rootdir_arr[i].filename, FS_FILENAME_LEN);
			printf("file: %s, ", filename);
			printf("size: %d, ", rootdir_arr[i].file_size);
			printf("data_blk: %d\n", rootdir_arr[i].first_data_block_index);
		}
	}
	return 0;
}

int fs_open(const char *filename)
{
	int filename_exists = 0;
	int filename_rootdir_inx;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		// Check if filename exists in root directory or not
		if (!(strcmp((char*)&rootdir_arr[i].filename, filename))) { 
			filename_exists = 1;
			filename_rootdir_inx = i;
			break;
		}
	}

	// Check if no fs is mounted, if invalid filename, or if filename doesn't exist
	if (!FS_mounted || &filename[0] == NULL || strlen(filename) >= FS_FILENAME_LEN || !filename_exists) {
		return -1;
	}

	// Find first open FD, and also check if the FD table is already full
	int next_open_fd_index;
	int fd_table_full = 0;
	for (int i = 0 ; i < FS_OPEN_MAX_COUNT ; ++i) {
		if (!fd_table[i].used) {
			next_open_fd_index = i;
			break;
		}
		if (i == FS_OPEN_MAX_COUNT - 1) {
			fd_table_full = 1;
			break;
		}
	}

	if (fd_table_full) {
		return -1;
	}

	fd_table[next_open_fd_index].used = 1;
	fd_table[next_open_fd_index].root_dir_index = filename_rootdir_inx;
	fd_table[next_open_fd_index].offset = 0;
	
	//fprintf(stderr, "FD returned for file %s is: %d\n", filename, next_open_fd_index);
	return next_open_fd_index;
}

int fs_close(int fd)
{
	// Check if no FS is mounted or if FD is out of bounds
	if (!FS_mounted || fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
		return -1;
	}

	// Check if FD not currently open (separate if statement so we don't try to access out of bounds)
	if (!fd_table[fd].used) {
		return -1;
	}

	fd_table[fd].used = 0;

	return 0;
}

int fs_stat(int fd)
{
	// Check if no FS is mounted or if FD is out of bounds
	if (!FS_mounted || fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
		return -1;
	}

	// Check if FD not currently open (separate if statement so we don't try to access out of bounds)
	if (!fd_table[fd].used) {
		return -1;
	}

	return (rootdir_arr[fd_table[fd].root_dir_index].file_size);
}

int fs_lseek(int fd, size_t offset)
{
	// Check if no FS is mounted or if FD is out of bounds
	if (!FS_mounted || fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
		return -1;
	}

	// Check if FD not currently open or if offset > current file size
	if (!fd_table[fd].used || (fd_table[fd].used && offset > fs_stat(fd))) {
		return -1;
	}

	fd_table[fd].offset = offset;

	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	// Check if no FS is mounted or if FD is out of bounds
	if (!FS_mounted || fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
		return -1;
	}

	// Check if FD is not currently open or if buf is NULL
	if (!fd_table[fd].used || buf == NULL) {
		return -1;
	}	
}

int fs_read(int fd, void *buf, size_t count)
{
	// Check if no FS is mounted or if FD is out of bounds
	if (!FS_mounted || fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
		return -1;
	}

	// Check if FD is not currently open or if buf is NULL
	if (!fd_table[fd].used || buf == NULL) {
		return -1;
	}

	int rootdir_idx = fd_table[fd].root_dir_index;
	size_t total_num_blocks = (rootdir_arr[rootdir_idx].file_size / (BLOCK_SIZE + 1)) + 1;
	size_t blocks_left_to_read = total_num_blocks - (fd_table[fd].offset / BLOCK_SIZE);
	size_t bytes_read = 0;
	size_t bytes_remaining = count;
	int data_blk_to_read = superblk.data_block_start_index + return_data_block(fd);	
	int data_blk_offset = superblk.data_block_start_index;

	// Go through all data blocks until there are no more data blocks to read
	while (1) {
		if (!blocks_left_to_read || !bytes_remaining) {
			break;
		}
		
		// Create a bounce buffer that stores entire data block
		char bounce_buf[BLOCK_SIZE];
		int readret = block_read(data_blk_to_read, &bounce_buf);
		if (readret == -1) {
			fprintf(stderr, "Could not read from disk (fs_read)\n");
			return -1;
		}

		size_t offset_distance = fd_table[fd].offset % BLOCK_SIZE;
		size_t bytes_just_read;

		// printf("BEFORE - blocks_left_to_read: %ld, bytes_remaining: %ld, bytes_read: %ld, fd_table[fd].offset: %ld, offset_distance: %ld\n", blocks_left_to_read, bytes_remaining, bytes_read, fd_table[fd].offset, offset_distance);

		if ((bytes_remaining + offset_distance) > BLOCK_SIZE) {
			// Need to read another data block
			memcpy((void*)(buf + bytes_read), (void*)(bounce_buf + offset_distance), BLOCK_SIZE - offset_distance);
			bytes_just_read = BLOCK_SIZE - offset_distance;
			bytes_read += bytes_just_read;
			bytes_remaining -= bytes_just_read;
			
			// Locate next data block index from FAT
			data_blk_to_read = get_next_data_blk(fd, data_blk_to_read - data_blk_offset);
			data_blk_to_read += data_blk_offset;
		} else {
			// Last data block, read remaining bytes
			memcpy((void*)(buf + bytes_read), (void*)(bounce_buf + offset_distance), bytes_remaining % (BLOCK_SIZE+1));
			bytes_just_read = bytes_remaining % (BLOCK_SIZE+1);
			bytes_read += bytes_just_read;
			bytes_remaining = 0;
		}

		// Update read status variables
		fd_table[fd].offset += bytes_just_read;
		blocks_left_to_read -= 1;
		// printf("AFTER - blocks_left_to_read: %ld, bytes_remaining: %ld, bytes_read: %ld, offset_distance: %ld\n", blocks_left_to_read, bytes_remaining, bytes_read, offset_distance);
	}
////////////////////////////////////
	// char bounce_buf[BLOCK_SIZE];
	// size_t offset_distance = fd_table[fd].offset % BLOCK_SIZE;
	// int readret = block_read(data_blk_to_read, &bounce_buf);
	// if (readret == -1) {
	// 	fprintf(stderr, "Could not read from disk (fs_read)\n");
	// 	return -1;
	// }
	// memcpy(buf, (void*)bounce_buf + offset_distance, bytes_remaining);
	// bytes_read = bytes_remaining;
////////////////////////////////////
	// char bounce_buf[BLOCK_SIZE];
	// int readret = block_read(data_blk_to_read, &bounce_buf);
	// if (readret == -1) {
	// 	fprintf(stderr, "Could not read from disk (fs_read)\n");
	// 	return -1;
	// }

	// printf("bounce_buf (read block %d):\n", data_blk_to_read);
	// for (int i = 0; i < count; i++) {
	// 	printf("%d ", bounce_buf[i]);
	// }
	// printf("Read %ld bytes from file. Compared %ld correct.\n", bytes_read, bytes_read);
	// memcpy(buf, (void*)bounce_buf + fd_table[fd].offset, count);
	//fd_table[fd].offset += count;
	return bytes_read;
}

