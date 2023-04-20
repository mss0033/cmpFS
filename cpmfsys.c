#include "cpmfsys.h"
#include "diskSimulator.h"

bool freeList[NUM_BLOCKS];

DirStructType *mkDirStruct(int index, uint8_t *e) {
    if (index < 0 || index >= EXTENT_SIZE) {
        return NULL; // Invalid index
    }

    DirStructType *dir_struct = (DirStructType *)malloc(sizeof(DirStructType));
    if (!dir_struct) {
        return NULL; // Memory allocation failed
    }

    uint8_t *extent_start = e + (index * EXTENT_SIZE);
    dir_struct->status = extent_start[0];
    memcpy(dir_struct->name, extent_start + 1, 8);
    dir_struct->name[8] = '\0';
    memcpy(dir_struct->extension, extent_start + 9, 3);
    dir_struct->extension[3] = '\0';
    dir_struct->XL = extent_start[12];
    dir_struct->BC = extent_start[13];
    dir_struct->XH = extent_start[14];
    dir_struct->RC = extent_start[15];
    memcpy(dir_struct->blocks, extent_start + 16, BLOCKS_PER_EXTENT);

    return dir_struct;
}

void writeDirStruct(DirStructType *d, uint8_t index, uint8_t *e) {
    if (!d || index >= EXTENT_SIZE) {
        return; // Invalid input
    }

    uint8_t *extent_start = e + (index * EXTENT_SIZE);
    extent_start[0] = d->status;
    memcpy(extent_start + 1, d->name, 8);
    memcpy(extent_start + 9, d->extension, 3);
    extent_start[12] = d->XL;
    extent_start[13] = d->BC;
    extent_start[14] = d->XH;
    extent_start[15] = d->RC;
    memcpy(extent_start + 16, d->blocks, BLOCKS_PER_EXTENT);
}

void makeFreeList() {
    // Initialize all blocks as free
    for (int i = 0; i < NUM_BLOCKS; i++) {
        freeList[i] = true;
    }

    // Block 0 is always in use as it contains the directory
    freeList[0] = false;

    uint8_t block0[1024];
    blockRead(block0, 0);

    // Iterate through all extents in block 0
    for (int i = 0; i < EXTENT_SIZE; i++) {
        DirStructType *dir_struct = mkDirStruct(i, block0);

        // If the extent is not used, continue to the next extent
        if (dir_struct->status == 0xe5) {
            free(dir_struct);
            continue;
        }

        // Iterate through all blocks in the extent and mark them as used in the freeList
        for (int j = 0; j < BLOCKS_PER_EXTENT; j++) {
            uint8_t block = dir_struct->blocks[j];

            // If the block number is valid and not a hole in the file, mark it as used
            if (block > 0 && block < NUM_BLOCKS) {
                freeList[block] = false;
            }
        }

        free(dir_struct);
    }
}

void printFreeList() {
    for (int i = 0; i < NUM_BLOCKS; i++) {
        // Print the 2-digit hex address of the first block in the row
        if (i % 16 == 0) {
            printf("%02x: ", i);
        }

        // Print a '*' for used blocks and a '.' for free blocks
        printf("%c ", freeList[i] ? '.' : '*');

        // Print a newline character after every 16 blocks
        if (i % 16 == 15) {
            printf("\n");
        }
    }
}

int findExtentWithName(char *name, uint8_t *block0) {
    if (!name || strlen(name) == 0) {
        return -1; // Invalid name
    }

    for (int i = 0; i < EXTENT_SIZE; i++) {
        DirStructType *dir_struct = mkDirStruct(i, block0);

        // If the extent is unused, continue to the next extent
        if (dir_struct->status == 0xe5) {
            free(dir_struct);
            continue;
        }

        // Check if the name matches the extent's name and extension
        if (strncmp(name, dir_struct->name, 8) == 0 && strncmp(name + 9, dir_struct->extension, 3) == 0) {
            int found_index = i;
            free(dir_struct);
            return found_index;
        }

        free(dir_struct);
    }

    return -1; // Name not found
}

bool checkLegalName(char *name) {
    if (!name || strlen(name) == 0) {
        return false; // Invalid name
    }

    char *dot_position = strchr(name, '.');
    size_t name_len = dot_position ? (size_t)(dot_position - name) : strlen(name);
    size_t ext_len = dot_position ? strlen(dot_position + 1) : 0;

    // Check if the name or extension is too long
    if (name_len > 8 || ext_len > 3) {
        return false;
    }

    // Check for illegal characters in the name and extension
    for (size_t i = 0; i < name_len; i++) {
        if (!((name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= '0' && name[i] <= '9') || name[i] == '_')) {
            return false;
        }
    }

    for (size_t i = 0; i < ext_len; i++) {
        if (!((dot_position[i + 1] >= 'A' && dot_position[i + 1] <= 'Z') ||
              (dot_position[i + 1] >= 'a' && dot_position[i + 1] <= 'z') ||
              (dot_position[i + 1] >= '0' && dot_position[i + 1] <= '9') || dot_position[i + 1] == '_')) {
            return false;
        }
    }

    return true;
}

void cpmDir() {
    uint8_t block0[1024];
    blockRead(block0, 0);

    for (int i = 0; i < EXTENT_SIZE; i++) {
        DirStructType *dir_struct = mkDirStruct(i, block0);

        // If the extent is unused, continue to the next extent
        if (dir_struct->status == 0xe5) {
            free(dir_struct);
            continue;
        }

        // Calculate file size
        uint16_t extent_number = ((dir_struct->XH & 0x1F) << 5) | (dir_struct->XL & 0x1F);
        uint32_t file_size = (extent_number * BLOCKS_PER_EXTENT * 1024) + (dir_struct->RC * 128) + dir_struct->BC;

        // Print file name, extension, and size
        printf("%-8s.%-3s %u\n", dir_struct->name, dir_struct->extension, file_size);

        free(dir_struct);
    }
}

int cpmRename(char *oldName, char *newName) {
    if (!checkLegalName(oldName) || !checkLegalName(newName)) {
        return -2; // Invalid filename
    }

    uint8_t block0[1024];
    blockRead(block0, 0);

    // Check if the newName already exists
    if (findExtentWithName(newName, block0) != -1) {
        return -3; // Destination filename already exists
    }

    // Find the extent with oldName
    int old_extent_index = findExtentWithName(oldName, block0);
    if (old_extent_index == -1) {
        return -1; // Source file not found
    }

    // Rename the file
    DirStructType *dir_struct = mkDirStruct(old_extent_index, block0);
    strncpy(dir_struct->name, newName, 8);
    strncpy(dir_struct->extension, newName + 9, 3);

    // Write the updated DirStructType back to block 0
    writeDirStruct(dir_struct, old_extent_index, block0);
    blockWrite(block0, 0);

    free(dir_struct);

    return 0; // Normal completion
}

int cpmDelete(char *name) {
    if (!checkLegalName(name)) {
        return -2; // Invalid filename
    }

    uint8_t block0[1024];
    blockRead(block0, 0);

    // Find the extent with the specified name
    int extent_index = findExtentWithName(name, block0);
    if (extent_index == -1) {
        return -1; // File not found
    }

    // Get the DirStructType for the found extent
    DirStructType *dir_struct = mkDirStruct(extent_index, block0);

    // Update the free list
    makeFreeList();
    for (int i = 0; i < BLOCKS_PER_EXTENT; i++) {
        if (dir_struct->blocks[i] != 0) {
            freeList[dir_struct->blocks[i]] = true;
        }
    }

    // Mark the extent as unused
    dir_struct->status = 0xe5;

    // Write the updated DirStructType back to block 0
    writeDirStruct(dir_struct, extent_index, block0);
    blockWrite(block0, 0);

    free(dir_struct);

    return 0; // Normal completion
}

int cpmCopy(char *oldName, char *newName) {
    // TODO: Implement this function
    return 0;
}

int cpmOpen(char *fileName, char mode) {
   // TODO: Implement this function
}

int cpmClose(int filePointer) {
    // TODO: Implement this function
    return 0;
}

int cpmRead(int pointer, uint8_t *buffer, int size) {
    // TODO: Implement this function
    return 0;
}

int cpmWrite(int pointer, uint8_t *buffer, int size) {
    // TODO: Implement this function
    return 0;
}