#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "fs.h"
#include "disk.h"


#define MAX_FILES 64
#define MAX_FILDES 32
#define FILE_CHARS 15


int mounted = 0; 
typedef struct 
{
int fat_idx; // First block of the FAT
int fat_len; // Length of FAT in blocks
int dir_idx; // First block of directory
int dir_len; // Length of directory in blocks
int data_idx; // First block of file-data
} superblock; 

typedef struct {
int used; // Is this file-”slot” in use
char name [FILE_CHARS + 1]; // DOH!
int size; // file size
int head; // first data block of file
int ref_cnt;
// how many open file descriptors are there?
// ref_cnt > 0 -> cannot delete file
} dir_entry; 

typedef struct 
{
    dir_entry entires[MAX_FILES]; 
    int count; 
} directory; 

typedef struct {
int used; // fd in use
int file; // the first block of the file
// (f) to which fd refers too
int offset; // position of fd within f
} file_descriptor; 


superblock fs;
file_descriptor filDes[MAX_FILDES]; // 32
int *FAT; // Will be populated with the FAT data
dir_entry *DIR; // Will be populated with the directory data


int make_fs(char *disk_name) {

    if(mounted)
        return -1;

    if(make_disk(disk_name) == -1)
        return -1; 

    if(open_disk(disk_name) == -1)
        return -1; 

    fs.dir_idx = 1; // directory starts at block 1
    fs.dir_len = 1; // dir length is 1 block
    fs.fat_idx = fs.dir_idx + fs.dir_len; // FAT comes after directory
    fs.fat_len = DISK_BLOCKS / 2 - 1; // FAT takes up half of all disk blocks
    fs.data_idx = fs.fat_idx + fs.fat_len; // data blocks start after FAT

    char tempBuf[BLOCK_SIZE] = {0};     // writing superblock to disk
    memcpy(tempBuf, &fs, sizeof(superblock)); 
    if(block_write(0, tempBuf) < 0) {
        fprintf(stderr, "Error occurred in make_fs: failed to write superblock to disk\n");
        close_disk(); 
        return -1; 
    }

    char blockBuf[BLOCK_SIZE] = {0};    // initialize disk data blocks
    int i = 1; 
    for(; i < fs.data_idx; i++) {
        if(block_write(i, blockBuf) < 0) {
            fprintf(stderr, "Error occurred in make_fs: failed to initialize metadata block %d\n", i); 
            close_disk(); 
            return -1; 
        }
    }

    if(close_disk() < 0) {
        fprintf(stderr, "Error occurred in make_fs: failed to close disk\n"); 
        return -1; 
    }
    
    return 0; 
}


int mount_fs(char *disk_name) {
    if(mounted)
        return -1; 

    if(open_disk(disk_name) < 0)
        return -1;

    char temp[BLOCK_SIZE];

    if(block_read(0, temp) < 0)     // get superblock from disk
        return -1;
    memcpy(&fs, temp, sizeof(superblock));  // copy superblock data to fs

    
    FAT = (int *)malloc(fs.fat_len * BLOCK_SIZE);   // allocate FAT
    if(block_read(fs.fat_idx, (char*)FAT) < 0) {    
        free(FAT);
        return -1;
    }


    DIR = (dir_entry*) malloc(fs.dir_len * BLOCK_SIZE); // allocate directory
    if(block_read(fs.dir_idx, (char*)DIR) < 0) {
        free(FAT);
        free(DIR);
        return -1;
    }

    mounted = 1; 
    return 0;     
}

int umount_fs(char *disk_name) {

    if(!mounted) {
        printf("mounted\n");
        return -1; 
    }

    char tempBuf[BLOCK_SIZE] = {0};     // write superblock to block 0
    memcpy(tempBuf, &fs, sizeof(superblock));
    if(block_write(0, tempBuf) < 0)  {   // if unable to write superblock to disk, return an error
        printf("BlockWR1\n");
        return -1; 
    }

    
    char tempFatBuf[BLOCK_SIZE] = {0};         // write FAT to disk
    int numFatEntries = BLOCK_SIZE/sizeof(int); 
    int i = 0;
    for(; i < fs.fat_len; i++) {
        memcpy(tempFatBuf, FAT + (i * numFatEntries), BLOCK_SIZE);
        if(block_write(fs.fat_idx + i, tempFatBuf) < 0) {   // if unable to write FAT to disk, return an error
            printf("blockwritefat\n");
            return -1; 
        } 
    }

    char tempDirBuf[BLOCK_SIZE] = {0};      // write directory to the disk
    int numDirEntries = BLOCK_SIZE/sizeof(dir_entry); 
    i = 0; 
    for(; i < fs.dir_len; i++) {
        memcpy(tempDirBuf, DIR + (i * numDirEntries), BLOCK_SIZE); 
        if(block_write(fs.dir_idx + i, tempDirBuf) < 0) {       // if unable to write directory to disk, return an error
            printf("blockwriteDir\n");
            return -1; 
        }
    }

    if(close_disk() < 0) {  // if unable to close disk, return an error
        printf("closedisk");
        return -1;
    }

    mounted = 0; 
    return 0; 
}


int fs_open(char *name) {

    if(!mounted)
        return -1; 


    int directoryIdx, foundFile = 0; 
    int i = 0; 
    for(; i < MAX_FILES; i++) {     // find if the file exists
        if(strcmp(DIR[i].name, name) == 0 && DIR[i].used) {
        directoryIdx = i; 
        foundFile = 1;
        break; 
        }
    }

    if(!foundFile || directoryIdx == MAX_FILES)  // if a file with 'name' cannot be found, return an error
        return -1; 

    int fildesIdx, foundOpenFd = 0; 
    i = 0; 
    for(; i < MAX_FILDES; i++) {    // find an unused filed descriptor
        if(filDes[i].used == 0) {
            fildesIdx = i; 
            foundOpenFd = 1;
            break; 
        }
    }

    if(!foundOpenFd || fildesIdx == MAX_FILDES)    // if max file descriptors are in use, return an error
        return -1; 


    filDes[fildesIdx].offset = 0; // seek pointer set to 0
    filDes[fildesIdx].file = directoryIdx; //  first data block of the file 
    filDes[fildesIdx].used = 1; // file descriptor in use

    if(DIR[fildesIdx].ref_cnt > 0) {
        DIR[fildesIdx].ref_cnt++;
    } else {
        DIR[fildesIdx].ref_cnt = 1;
    }

    return fildesIdx; // return file descriptor 

}


int fs_close(int fildes) {

    if(!mounted)
        return -1; 

    if(filDes[fildes].used == 0) {    // if file descriptor is not open, return an error
        return -1; 
    }

    DIR[filDes[fildes].file].ref_cnt--; // decrement the ref count of the file

    if(DIR[filDes[fildes].file].ref_cnt == 0)
        filDes[fildes].used = 0;      // if no references, fildes is no longer used

    return 0;
}

int fs_create(char *name) {

    if(!mounted)
        return -1; 

    int fileFound = 0; 

    int i = 0; 
    for(; i < MAX_FILES; i++) {
        if(strcmp(DIR[i].name, name) == 0) {    // find if file name already exists
            fileFound = 1; 
        }
    }

    if(fileFound)
        return -1; 

    if(strlen(name) > FILE_CHARS + 1)   // if length of name exceeds max name length, return an error
        return -1; 

    i = 0; 
    int dirIdx, openSpotFound = 0;  
    for(; i < MAX_FILES; i++) {
        if(DIR[i].used == 0) {
            dirIdx = i; 
            openSpotFound = 1;
            break; 
        }
    }

    if(!openSpotFound || dirIdx == MAX_FILES)  // if there are already 64 files in the directory, return an error
        return -1; 

    
    strcpy(DIR[dirIdx].name, name);     // initialize directory entry 
    DIR[dirIdx].size = 0; 
    DIR[dirIdx].ref_cnt = 0; 
    DIR[dirIdx].used = 1; 
    DIR[dirIdx].head = -1; 


    int firstBlock;     // find first available block for the file
    i = 0; 
    for(; i < fs.fat_len; i++) {
        if(FAT[i] == 0) {
            firstBlock = i; 
            break; 
        }
    }

    DIR[dirIdx].head = firstBlock;    
    FAT[firstBlock] = -1; 

    return 0; 

}


int fs_delete(char *name) {

    if(!mounted)
        return -1; 


    int i = 0, fileFound = 0, dirIdx; 
    for(; i < MAX_FILES; i++) {
        if(strcmp(DIR[i].name, name) == 0) {    // find if file name exists
            dirIdx = i;
            fileFound = 1; 
        }
    }

    if(!fileFound)  // if there is no file found with 'name', return an error
        return -1; 

    if(DIR[dirIdx].ref_cnt > 0) // if file is open, return an error
        return -1; 

    DIR[dirIdx].used = 0; 
    
    int firstBlock = DIR[dirIdx].head; 

    while(firstBlock != EOF) {  // free blocks used by the file
        int temp = FAT[firstBlock];
        FAT[firstBlock] = -1; 
        firstBlock = temp; 
    }

    FAT[firstBlock] = -1;

    memset(&DIR[dirIdx], 0, sizeof(dir_entry)); // clear directory entry
    return 0; 

}


int fs_read(int fildes, void *buf, size_t nbyte) {  // reads data from fd into a buffer

    if(!mounted)
        return -1; 

    if (fildes < 0 || fildes >= MAX_FILDES || !filDes[fildes].used) {   // valid file descriptor?
        return -1;  
    }

    size_t readBytes = 0;
    int readIdx = 0;
    
    while(fildes != -1){    // read to the end of the file
        if(nbyte < readBytes + BLOCK_SIZE){ // dont need to copy a full block

            char tempBuf[BLOCK_SIZE];
            if(block_read(fildes, tempBuf) == -1){ 
                return -1;
            }
            memcpy(buf + readIdx * BLOCK_SIZE, tempBuf, nbyte - readBytes); // copy necessary data to the argument buffer
            readBytes += nbyte - readBytes;
            return readBytes;

        } else if(block_read(fildes, buf + readIdx * BLOCK_SIZE) == -1){ 
            return -1;
        }
        readBytes += BLOCK_SIZE;
        fildes = FAT[fildes];   // move to the next block
        readIdx++;
    }


    return readBytes;   // return number of bytes read
}




int fs_write(int fildes, void *buf, size_t nbyte) {

    if (!mounted)
        return -1;

    if (fildes < 0 || fildes >= MAX_FILDES || !filDes[fildes].used)  
        return -1;

    file_descriptor *fd = &filDes[fildes];
    int offset = fd->offset;
    size_t bytesWritten = 0;
    size_t remainingBytes = nbyte;

    while (remainingBytes > 0) {    // loop until all bytes are written
        int block = (offset + remainingBytes) / BLOCK_SIZE; // which block to write to
        int blockOffset = offset % BLOCK_SIZE;  // offset with the block

        int bytesInBlock = BLOCK_SIZE - blockOffset;    // space remaining 
        int writeBytes = (remainingBytes < bytesInBlock) ? remainingBytes : bytesInBlock; // how many bytes to write

        if (offset + bytesWritten > DIR[fd->file].size) {   // check if file should be extended
            if (DIR[fd->file].head == -1) {
                int i = 0; 
                for(; i < DISK_BLOCKS; i++) {
                    if(FAT[i] == 0) {
                        FAT[i] = -1;
                        DIR[fd->file].head = i;
                    }
                }
            }

            int currentBlock = DIR[fd->file].head;
            int i = 0;
            for (; i < block - 1 && currentBlock != -1; i++) {
                currentBlock = FAT[currentBlock];   // find correct block
            }

            int newBlock;
            if (currentBlock != -1) {   // find a free block
                int i = 0;
                for(; i < DISK_BLOCKS; i++) {
                    if(FAT[i] == 0) {
                        FAT[i] = -1;
                        newBlock = i;
                    }
                }
                if (newBlock == -1)     // no free blocks
                    return bytesWritten;  

                FAT[currentBlock] = newBlock;
            }
        }

        if (block_write(block, buf + bytesWritten) < 0) {
            return -1;  
        }

        bytesWritten += writeBytes;
        remainingBytes -= writeBytes;
        offset += writeBytes;
        fd->offset = offset;  

        if (fd->offset > DIR[fd->file].size) {  // if file was extended update size
            DIR[fd->file].size = fd->offset;
        }
    }

    return bytesWritten;
}

int fs_get_filesize(int fildes) {

    if(!mounted)
        return -1; 

    if(!filDes[fildes].used || fildes < 0 || fildes > MAX_FILDES) // if file descriptor is not valid, return an error
        return -1; 

    file_descriptor *fd = &filDes[fildes];

    dir_entry *en = &DIR[fd->file]; 
    
    return en->size;    // return file size

}

int fs_listfiles(char ***files) {

    if(!mounted)
        return -1;

    int fileCount = 0; 
    int i = 0; 
    for(; i < MAX_FILES; i++) {
        if(DIR[i].used == 1) {
            fileCount++;
        }
    }

    char **fileList = malloc((fileCount + 1) * sizeof(char*));  // allocate memory for file list

    i = 0; 
    int index = 0; 
    for(; i < MAX_FILES; i++) { // add each file to the list
        if(DIR[i].used == 1) {
            fileList[index] = DIR[i].name; 
            index++;
        }
    }

    fileList[index] = NULL;

    *files = fileList;  // return the file list

    return 0;
}

int fs_lseek(int fildes, off_t offset) {

    if(!mounted)
        return -1; 

    if(!filDes[fildes].used || fildes < 0 || fildes > MAX_FILDES) // if file descriptor is not valid, return an error
        return -1; 

    int fileSize = fs_get_filesize(fildes);

    if(offset < 0 || offset > fileSize);
        return -1; 

    filDes[fildes].offset = offset; // update file pointer offset

    return 0;
}

int fs_truncate(int fildes, off_t length) {

    if (!mounted)
        return -1;

    if (!filDes[fildes].used || fildes < 0 || fildes >= MAX_FILDES) // if file descriptor is not valid, return an error
        return -1;

    int fileSize = fs_get_filesize(fildes);

    if (length < 0 || length > fileSize)
        return -1;

    if (length < fileSize) {
        int currentBlock = DIR[filDes[fildes].file].head;
        int blocksToKeep = (length + BLOCK_SIZE - 1) / BLOCK_SIZE; // number of blocks to keep
        int blockCount = 0;

        while (currentBlock != -1 && blockCount < blocksToKeep) {
            currentBlock = FAT[currentBlock];
            blockCount++;
        }

                                       
        while (currentBlock != -1) { // free remaining blocks
            int nextBlock = FAT[currentBlock];
            FAT[currentBlock] = -1; 
            currentBlock = nextBlock;
        }
    }

    DIR[filDes[fildes].file].size = length; // update file size 
    return 0;
}


