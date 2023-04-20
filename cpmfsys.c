/**
 * Author: Matthew Shiplett
 * Date last modified: April 20th, 2023
 * This file contains the implementations of the functions defined in cpmfsys.h
 */
#include "cpmfsys.h"
#include "diskSimulator.h"

// Initialize a list of free blocks
bool freeList[NUM_BLOCKS];

void trim_string(char *str) {
    // Trim leading whitespace
    char *start = str;
    while (isspace((unsigned char)*start)) {
        start++;
    }

    // If the string only contained whitespace, update the original string and return
    if (*start == '\0') {
        *str = '\0';
        return;
    }

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }

    // Null-terminate the trimmed string
    end[1] = '\0';

    // If the original string had leading whitespace, shift the trimmed string to the beginning
    if (start > str) {
        memmove(str, start, end - start + 2);
    }
}

/**
 * Creates a DirStructType from a given index of the given extent
 */
DirStructType *mkDirStruct(int index, uint8_t *e) 
{
    if (index < 0 || index >= EXTENT_SIZE) 
    {
        return NULL; // Invalid index
    }

    DirStructType *dir_struct = (DirStructType *)malloc(sizeof(DirStructType));
    if (!dir_struct) 
    {
        return NULL; // Memory allocation failed
    }

    const char space[1] = " ";

    uint8_t *extent_start = e + (index * EXTENT_SIZE);
    dir_struct->status = extent_start[0];
    strncpy(dir_struct->name, (char *)&extent_start[1], 8);
    trim_string(dir_struct->name);
    //dir_struct->name[8] = '\0';
    strncpy(dir_struct->extension, (char *)&extent_start[9], 3);
    trim_string(dir_struct->extension);
    //dir_struct->extension[3] = '\0';
    dir_struct->XL = extent_start[12];
    dir_struct->BC = extent_start[13];
    dir_struct->XH = extent_start[14];
    dir_struct->RC = extent_start[15];
    memcpy(dir_struct->blocks, extent_start + 16, BLOCKS_PER_EXTENT);

    return dir_struct;
}

/**
 * Wrties a DirStructType to a given index of a given extent
 */
void writeDirStruct(DirStructType *d, uint8_t index, uint8_t *e) 
{
    if (!d || index >= EXTENT_SIZE) 
    {
        return; // Invalid input
    }

    uint8_t *extent_start = e + (index * EXTENT_SIZE);
    extent_start[0] = d->status;
    strncpy((char *)&extent_start[1], d->name, 8);
    strncpy((char *)&extent_start[9], d->extension, 3);
    extent_start[12] = d->XL;
    extent_start[13] = d->BC;
    extent_start[14] = d->XH;
    extent_start[15] = d->RC;
    memcpy(extent_start + 16, d->blocks, BLOCKS_PER_EXTENT);
}

/**
 * Generates the list of free blocks
 */
void makeFreeList() 
{
    // Initialize all blocks as free
    for (int i = 0; i < NUM_BLOCKS; i++) 
    {
        freeList[i] = true;
    }

    // Block 0 is always in use as it contains the directory
    freeList[0] = false;

    uint8_t block0[BLOCK_SIZE];
    blockRead(block0, 0);

    // Iterate through all extents in block 0
    int i;
    for (i = 0; i < EXTENT_SIZE; i++) 
    {
        DirStructType *dir_struct = mkDirStruct(i, block0);

        // If the extent is not used, continue to the next extent
        if (dir_struct->status == 0xe5) 
        {
            free(dir_struct);
            continue;
        }

        // Iterate through all blocks in the extent and mark them as used in the freeList
        int j;
        for (j = 0; j < BLOCKS_PER_EXTENT; j++) 
        {
            uint8_t block = dir_struct->blocks[j];

            // If the block number is valid and not a hole in the file, mark it as used
            if (block > 0 && block < NUM_BLOCKS) 
            {
                freeList[block] = false;
            }
        }

        free(dir_struct);
    }
}

/**
 * Prints the list of free blocks 
 */
void printFreeList() 
{
    printf("FREE BLOCK LIST: (* means in-use)\n");
    int i;
    for (i = 0; i < NUM_BLOCKS; i++) 
    {
        // Print the 2-digit hex address of the first block in the row
        if (i % 16 == 0) 
        {
            printf("%02x: ", i);
        }

        // Print a '*' for used blocks and a '.' for free blocks
        printf("%c ", freeList[i] ? '.' : '*');

        // Print a newline character after every 16 blocks
        if (i % 16 == 15) 
        {
            printf("\n");
        }
    }
}

/**
 * Finds an extent with a given file name and returns the index
 */
int findExtentWithName(char *name, uint8_t *block0) 
{
    if (!name || strlen(name) == 0) 
    {
        return -1; // Invalid name
    }

    int i;
    for (i = 0; i < EXTENT_SIZE; i++) 
    {
        DirStructType *dir_struct = mkDirStruct(i, block0);

        // If the extent is unused, continue to the next extent
        if (dir_struct->status == 0xe5) 
        {
            free(dir_struct);
            continue;
        }

        // Check if the name matches the extent's name and extension
        char* file_name = strdup(dir_struct->name);
        if (strlen(dir_struct->extension) != 0)
        {   
            strcat(file_name, ".");
            strcat(file_name, dir_struct->extension);
        }
        if (strcmp(name, file_name) == 0) 
        {
            int found_index = i;
            free(dir_struct);
            return found_index;
        }

        free(dir_struct);
    }

    return -1; // Name not found
}

/**
 * Checks if the given char* contians a valid name 
 */
bool checkLegalName(char *name) 
{
    if (!name || strlen(name) == 0) 
    {
        return false; // Invalid name
    }

    char *dot_position = strchr(name, '.');
    size_t name_len = dot_position ? (size_t)(dot_position - name) : strlen(name);
    size_t ext_len = dot_position ? strlen(dot_position + 1) : 0;

    // Check if the name or extension is too long
    if (name_len > 8 || ext_len > 3) 
    {
        return false;
    }

    // Check for illegal characters in the name and extension
    size_t i;
    for (i = 0; i < name_len; i++) 
    {
        if (!((name[i] >= 'A' && name[i] <= 'Z') 
            || (name[i] >= 'a' && name[i] <= 'z') 
            ||(name[i] >= '0' && name[i] <= '9') 
            || name[i] == '_')) 
        {
            return false;
        }
    }

    for (i = 0; i < ext_len; i++) 
    {
        if (!((dot_position[i + 1] >= 'A' && dot_position[i + 1] <= 'Z') 
            || (dot_position[i + 1] >= 'a' && dot_position[i + 1] <= 'z') 
            || (dot_position[i + 1] >= '0' && dot_position[i + 1] <= '9') 
            || dot_position[i + 1] == '_'))
        {
            return false;
        }
    }

    return true;
}

/**
 * Lists the current directory 
 */
void cpmDir() 
{
    printf("DIRECTORY LISTING\n");
    // Read the block0 aka the Extent
    uint8_t block0[BLOCK_SIZE];
    blockRead(block0, 0);
    
    int i;
    for (i = 0; i < EXTENT_SIZE; i++) 
    {
        DirStructType *dir_struct = mkDirStruct(i, block0);

        // If the extent is unused, continue to the next extent
        if (dir_struct->status == 0xe5) 
        {
            free(dir_struct);
            continue;
        }

        // Calculate the number of non-zero blocks used by the file
        int num_blocks = 0;
        int j;
        for (j = 0; j < BLOCKS_PER_EXTENT; j++) 
        {
            if (dir_struct->blocks[j] != 0) {
                num_blocks++;
            }
        }

        // Off by one error somewhere, haven't found it yet
        num_blocks--;

        // Calculate file size
        uint32_t file_size = (num_blocks * BLOCK_SIZE) + (dir_struct->RC * 128) + dir_struct->BC;

        // Print file name, extension, and size
        printf("%s.%s %u\n", dir_struct->name, dir_struct->extension, file_size);

        free(dir_struct);
    }
}

/**
 * Renames a file, if one with the matching old name is found 
 */
int cpmRename(char *oldName, char *newName) 
{
    if (!checkLegalName(oldName) || !checkLegalName(newName)) 
    {
        return -2; // Invalid filename
    }

    uint8_t block0[BLOCK_SIZE];
    blockRead(block0, 0);

    // Check if the newName already exists
    if (findExtentWithName(newName, block0) != -1) 
    {
        return -3; // Destination filename already exists
    }

    // Find the extent with oldName
    int old_extent_index = findExtentWithName(oldName, block0);
    if (old_extent_index == -1) 
    {
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

/**
 * Delets a file, if one with the matching name is found 
 */
int cpmDelete(char *name) 
{
    if (!checkLegalName(name)) 
    {
        printf("Error, illegal file name");
        return -2; // Invalid filename
    }

    uint8_t block0[BLOCK_SIZE];
    blockRead(block0, 0);

    // Find the extent with the specified name
    int extent_index = findExtentWithName(name, block0);
    if (extent_index == -1) 
    {
        printf("File not found");
        return -1; // File not found
    }

    // Get the DirStructType for the found extent
    DirStructType *dir_struct = mkDirStruct(extent_index, block0);
    if (dir_struct == NULL)
    {
        printf("Error creating DirStructType");
        return -5; // Error creating DirStructType
    }

    // Mark the extent as unused
    dir_struct->status = 0xe5;

    // Update the free list
    int i;
    for (i = 0; i < BLOCKS_PER_EXTENT; i++) 
    {
        if (dir_struct->blocks[i] != 0) 
        {
            freeList[dir_struct->blocks[i]] = true;
        }
    }

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