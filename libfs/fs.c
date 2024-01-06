#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

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

struct __attribute__ ((__packed__)) fd_entry {
	int used;
	int root_dir_index;
	size_t offset;
};

struct superblock superblk;
struct FAT_section FAT_nodes;
struct root_directory rootdir_arr[FS_FILE_MAX_COUNT];
struct fd_entry fd_table[FS_OPEN_MAX_COUNT];
bool FS_mounted = false;

// returns -1 if there is no empty entry accessible
// otherwise, returns the next empty FAT entry
int find_next_empty_entry(int num_data_blocks) {
	struct FAT_node* curr = FAT_nodes.start;
	uint16_t curr_fat_blk_idx = 1;
	size_t next_data_blk_idx;

	// Go through entries of FAT block to find next empty entry
	for (int i = 0; i < num_data_blocks; i++) {
		next_data_blk_idx = curr->entries[curr_fat_blk_idx % FB_ENTRIES_PER_BLOCK];
		if ((next_data_blk_idx != 0) && (i == superblk.num_data_blocks - 1)) {
			return -1;
		} else if (next_data_blk_idx == 0) {
			curr->entries[curr_fat_blk_idx % FB_ENTRIES_PER_BLOCK] = FAT_EOC;
			return curr_fat_blk_idx;
		}

		if (curr_fat_blk_idx % FB_ENTRIES_PER_BLOCK > next_data_blk_idx % FB_ENTRIES_PER_BLOCK) {
			// Another entry is reachable - advance to it
			curr = curr->next;
		}
		curr_fat_blk_idx++;
	}
	
	return curr_fat_blk_idx;
}

// returns index of data block where offset of fd is located (indexed by FAT table index, not overall block index)
size_t return_data_block(int fd, size_t count) {
	int root_dir_idx = fd_table[fd].root_dir_index;
	//how many data blocks past the first one 
	int num_iterations = fd_table[fd].offset / BLOCK_SIZE;
	size_t curr_data_blk_idx = rootdir_arr[root_dir_idx].first_data_block_index; 
	size_t next_data_blk_idx;

	// if this file is currently empty, we must allocate the first data block (if we have bytes to write)
	if (curr_data_blk_idx == FAT_EOC && count > 0) {
		struct FAT_node* curr = FAT_nodes.start;
		int curr_fat_blk_idx = find_next_empty_entry(superblk.num_data_blocks);

		if (curr_fat_blk_idx == -1) {
			return -1;
		}

		rootdir_arr[root_dir_idx].first_data_block_index = curr_fat_blk_idx;
		return curr_fat_blk_idx; 
	}
		
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
		next_data_blk_idx = curr->entries[curr_data_blk_idx % FB_ENTRIES_PER_BLOCK];
		if (next_data_blk_idx == FAT_EOC) {
			break;
		}
		//next_data_blk_idx = next_data_blk_idx % FB_ENTRIES_PER_BLOCK;
		if (curr_data_blk_idx % FB_ENTRIES_PER_BLOCK > next_data_blk_idx % FB_ENTRIES_PER_BLOCK) {
			// Assuming in-order FAT entries
			curr = curr->next;
		}
		curr_data_blk_idx = next_data_blk_idx;
	}
	return curr_data_blk_idx;
}

// returns -1 if there are no more open FAT entries
// otherwise, returns index of the new block allocated for fd.
int allocate_new_data_block(int fd, int current_last_data_blk) {
	// Locate appropriate FAT node
	int FAT_block_num_last_entry = current_last_data_blk / FB_ENTRIES_PER_BLOCK;
	struct FAT_node* curr = FAT_nodes.start;

	int curr_fat_blk_idx = find_next_empty_entry(superblk.num_data_blocks);

	if (curr_fat_blk_idx == -1) {
		return -1;
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

// returns FAT node pointer indicating next FAT node location
struct FAT_node* get_next_FAT_node(int current_data_blk) {
	int FAT_block_num_last_entry = current_data_blk / FB_ENTRIES_PER_BLOCK;
	int i = 0;
	struct FAT_node* curr = FAT_nodes.start;
	while(i != FAT_block_num_last_entry) {
		curr = curr->next;
		i++;
	}
	return curr;
}

// returns index of next data block after current_data_blk in file
int get_next_data_blk(int current_data_blk) {
	return get_next_FAT_node(current_data_blk)->entries[current_data_blk % FB_ENTRIES_PER_BLOCK];
}

// returns FAT node pointer indicaing location of specified data block
struct FAT_node* traverse_FAT_until_data_blk(int data_blk) {
	int node_num = data_blk / FB_ENTRIES_PER_BLOCK;
	struct FAT_node* curr = FAT_nodes.start;
	for (int i = 0; i < node_num; i++) {
		curr = curr->next;
	}
	return curr;
}

// inserts FAT_EOC into entry following writing operation
void insert_FAT_EOC(int data_blk_to_write) {
	struct FAT_node* curr = traverse_FAT_until_data_blk(data_blk_to_write);
	curr->entries[data_blk_to_write % FB_ENTRIES_PER_BLOCK] = FAT_EOC;
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

	// Initialize the fd table
	for (int i = 0 ; i < FS_OPEN_MAX_COUNT ; ++i) {
		fd_table[i].used = 0;
		fd_table[i].offset = 0;
	}

	FS_mounted = true;
	return 0;
}

int fs_umount(void) {
	// Check if there are any open fd's
	int all_fd_closed = 1;
	for (int i = 0 ; i < FS_OPEN_MAX_COUNT ; ++i) {
		if (fd_table[i].used == 1) {
			all_fd_closed = 0;
			break;
		}
	}
	
	// Check if no FS is mounted, if disk cannot be closed, or if there are still open file descriptors
	if (!FS_mounted || block_disk_count() == -1 || !all_fd_closed) {
		return -1;
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

	FS_mounted = false;
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
		
		// Go through all entries and count number of ones that are empty
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

int fs_create(const char *filename) {
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
	
	// Check if no FS mounted, if filename is invalid, if if given filename is too long, or if root directory alrady has the max # of files
	if (!FS_mounted || &filename[0] == NULL || strlen(filename) >= FS_FILENAME_LEN || num_rdir_files >= FS_FILE_MAX_COUNT) {
		return -1;
	}

	// Locate empty entry in root directory
	int empty_entry_idx = 0;
	while (rootdir_arr[empty_entry_idx].filename[0] != '\0') {
		empty_entry_idx++;
	}

	// Allocate space to store new filename string (2 bytes)
	int8_t *new_filename;
	new_filename = (int8_t*)strndup(filename, FS_FILENAME_LEN);
	
	// Create new & empty file with given filename at empty entry in root directory
	memcpy((int8_t*)&rootdir_arr[empty_entry_idx].filename, new_filename, FS_FILENAME_LEN);
	rootdir_arr[empty_entry_idx].file_size = 0;
	rootdir_arr[empty_entry_idx].first_data_block_index = FAT_EOC;
	
	return 0;
}

int fs_delete(const char *filename) {
	int filename_exists = 0;
	int filename_rootdir_idx;
	// Check if filename to delete already exists in root directory
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (!(strcmp((char*)&rootdir_arr[i].filename, filename))) { 
			filename_exists = 1;
			filename_rootdir_idx = i;
			rootdir_arr[i].filename[0] = '\0';
			break;
		}
	}
	
	// Check if no FS is mounted, if filename is invalid, or if filename does not exist in root directory
	if (!FS_mounted || &filename[0] == NULL || strlen(filename) >= FS_FILENAME_LEN || !filename_exists) {
		return -1;
	}

	int fd;
	for (int i = 0 ; i < FS_OPEN_MAX_COUNT ; ++i) {
		if (fd_table[i].root_dir_index == filename_rootdir_idx) {
			fd = i;
			break;
		}
	}

	// Check if filename is currently opened
	if (fd_table[fd].used) {
		return -1;
	}

	// For stored files that are not empty, calculate FAT block entry of index to delete from
	if (rootdir_arr[filename_rootdir_idx].first_data_block_index != FAT_EOC) {
		int FAT_block_num = rootdir_arr[filename_rootdir_idx].first_data_block_index / FB_ENTRIES_PER_BLOCK;
		int delete_FAT_inx = rootdir_arr[filename_rootdir_idx].first_data_block_index % FB_ENTRIES_PER_BLOCK;
		
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
	for (int i = 0 ; i < FS_OPEN_MAX_COUNT ; ++i) {
		if (!fd_table[i].used) {
			next_open_fd_index = i;
			break;
		}
		if (i == FS_OPEN_MAX_COUNT - 1) {
			return -1;
		}
	}

	fd_table[next_open_fd_index].used = 1;
	fd_table[next_open_fd_index].root_dir_index = filename_rootdir_inx;
	fd_table[next_open_fd_index].offset = 0;
	
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

	int rootdir_idx = fd_table[fd].root_dir_index;
	size_t total_num_blocks = (rootdir_arr[rootdir_idx].file_size / (BLOCK_SIZE + 1)) + 1;
	size_t blocks_left = total_num_blocks - (fd_table[fd].offset / BLOCK_SIZE);
	size_t total_bytes_written = 0;
	size_t bytes_remaining = count;
	int data_blk_offset = superblk.data_block_start_index;
	int data_blk_to_write = superblk.data_block_start_index + return_data_block(fd, count);
	int data_blk_to_write_offset_considered;
	int old_offset = fd_table[fd].offset;	

	// Keep writing as long as there are no more bytes to write
	while (1) {
		if (!bytes_remaining) {
			break;
		}

		// Create a bounce buffer that stores entire data block
		char bounce_buf[BLOCK_SIZE];
		if (!(data_blk_to_write >= superblk.data_block_start_index + superblk.num_data_blocks)) {
			// Data block allocated, read block
			int readret = block_read(data_blk_to_write, &bounce_buf);
			if (readret == -1) {
				fprintf(stderr, "Could not read from disk when creating bounce buffer (fs_write)\n");
				return -1;
			}
		}

		size_t offset_distance = fd_table[fd].offset % BLOCK_SIZE;
		void* writing_dest = bounce_buf + offset_distance;
		void* writing_src = buf + total_bytes_written;
		size_t num_bytes_writing;
		// size_t bytes_just_written;

		// Check if currently on last available data block of underlying disk
		if (data_blk_to_write + 1 >= superblk.data_block_start_index + superblk.num_data_blocks) {
			num_bytes_writing = BLOCK_SIZE - offset_distance;
			total_bytes_written += num_bytes_writing;
			bytes_remaining = 0;
			data_blk_to_write -= data_blk_offset;

			insert_FAT_EOC(data_blk_to_write);

			data_blk_to_write += data_blk_offset;
			fd_table[fd].offset += num_bytes_writing;
		}

		// Check if there is another block to write to after this one
		else if ((bytes_remaining + offset_distance) > BLOCK_SIZE) {
			num_bytes_writing = BLOCK_SIZE - offset_distance;
			total_bytes_written += num_bytes_writing;
			bytes_remaining -= num_bytes_writing;

			data_blk_to_write_offset_considered = data_blk_to_write - data_blk_offset;

			if (blocks_left == 1) {
				insert_FAT_EOC(data_blk_to_write_offset_considered);
				
				// Attempt to get another empty FAT data block if extension of allocated space inside filesystem is required
				int new_data_block = allocate_new_data_block(fd, data_blk_to_write_offset_considered);
				if (new_data_block == -1) {
					fd_table[fd].offset += num_bytes_writing;
					int new_bytes_written = total_bytes_written - rootdir_arr[fd_table[fd].root_dir_index].file_size;
					if (new_bytes_written > 0) {
						rootdir_arr[fd_table[fd].root_dir_index].file_size += (new_bytes_written + old_offset);
					}
					// No more empty FAT blocks available - stop writing
					return total_bytes_written;
				}
				
				// New FAT block allocation was successful - continue writing
				data_blk_to_write = new_data_block + data_blk_offset;
			} else {
				int last_data_block = data_blk_to_write;
				data_blk_to_write = get_next_data_blk(data_blk_to_write_offset_considered);
				struct FAT_node* curr = traverse_FAT_until_data_blk(data_blk_to_write_offset_considered);
				curr->entries[(last_data_block - data_blk_offset) % FB_ENTRIES_PER_BLOCK] = data_blk_to_write;
				data_blk_to_write += data_blk_offset;
			}
		}
		
		// Only need current block to finish writing
		else {
			num_bytes_writing = bytes_remaining;
			total_bytes_written += num_bytes_writing;
			bytes_remaining = 0;
			data_blk_to_write -= data_blk_offset;
			
			insert_FAT_EOC(data_blk_to_write);

			data_blk_to_write += data_blk_offset;
		}

		// Write to disk
		memcpy(writing_dest, writing_src, num_bytes_writing);
		block_write(data_blk_to_write, &bounce_buf);

		// Update write status variables in fd table
		fd_table[fd].offset += num_bytes_writing;
		
		// Decrement # of blocks left UNLESS we have already hit the final one, then keep it at 1
		if (blocks_left > 1) {
			blocks_left -= 1;
		}
	}
	
	// Update fd table once writing is finished
	int new_bytes_written = total_bytes_written - rootdir_arr[fd_table[fd].root_dir_index].file_size;
	if (new_bytes_written > 0) {
		rootdir_arr[fd_table[fd].root_dir_index].file_size += (new_bytes_written + old_offset);
	}
	
	return total_bytes_written;
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
	size_t total_bytes_read = 0;
	size_t bytes_remaining = count;
	int data_blk_to_read = superblk.data_block_start_index + return_data_block(fd, count);	
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
			fprintf(stderr, "Could not read from disk when creating bounce buffer (fs_read)\n");
			return -1;
		}

		size_t offset_distance = fd_table[fd].offset % BLOCK_SIZE;
		void* reading_dest = buf + total_bytes_read;
		void* reading_src = bounce_buf + offset_distance;
		size_t num_bytes_reading;

		// Check if need to read more bytes than what is left in blocks allocated for file,
		// or if reading will stop in the same data block that the file ends in
		if ((blocks_left_to_read == 1 && (bytes_remaining + offset_distance) > BLOCK_SIZE)
				|| (blocks_left_to_read == 1 && (rootdir_arr[rootdir_idx].file_size % BLOCK_SIZE) != 0 && (bytes_remaining + offset_distance) > (rootdir_arr[rootdir_idx].file_size % BLOCK_SIZE))) {
			num_bytes_reading = rootdir_arr[rootdir_idx].file_size % BLOCK_SIZE;
			bytes_remaining = 0;
		}

		// Otherwise, check if reading more data requires reading from another data block
		else if ((bytes_remaining + offset_distance) > BLOCK_SIZE) {
			// Read the entire block
			num_bytes_reading = BLOCK_SIZE - offset_distance;
			bytes_remaining -= num_bytes_reading;
			
			// Locate next data block index from FAT
			data_blk_to_read = get_next_data_blk(data_blk_to_read - data_blk_offset);
			data_blk_to_read += data_blk_offset;
		
		// Otherwise, read the remaining bytes from the current block to finish reading
		} else {
			num_bytes_reading = bytes_remaining % (BLOCK_SIZE + 1);
			bytes_remaining = 0;
		}

		// Read from disk
		memcpy(reading_dest, reading_src, num_bytes_reading);
		total_bytes_read += num_bytes_reading;

		// Update read status variables
		fd_table[fd].offset += num_bytes_reading;
		blocks_left_to_read -= 1;
	}

	return total_bytes_read;
}
