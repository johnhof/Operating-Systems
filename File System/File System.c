
/*------------------------------------------------------------------------------------------------------------------------------------------
 *DIRECTORIES ONLY (part 1)
 *John Hofrichter - jmh162
 *Nee Taylor - net9
 *------------------------------------------------------------------------------------------------------------------------------------------

	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.

*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define	MAX_FILES_IN_DIR (BLOCK_SIZE - (MAX_FILENAME + 1) - sizeof(int)) / \
	((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK BLOCK_SIZE

//the total number of bytes in the disk
#define DISK_SIZE 5242880


//number of bytes on disk / block size = number of blocks
#define NUMBER_OF_BLOCKS (DISK_SIZE/BLOCK_SIZE)

//size of bitmap in bytes (in this case, 1280)
#define BYTES_IN_MAP DISK_SIZE/BLOCK_SIZE/8

struct cs1550_directory_entry
{
	char dname[MAX_FILENAME	+ 1];	//the directory name (plus space for a nul)
	int nFiles;			//How many files are in this directory. 
					//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;			//file size
		long nStartBlock;		//where the first block is on disk
	} files[MAX_FILES_IN_DIR];		//There is an array of these
};

typedef struct cs1550_directory_entry cs1550_directory_entry;

struct cs1550_disk_block
{
	//And all of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;


/*------------------------------------------------------------------------------------------------------------------------------------------
 *parsePath
 * 		parse the file path into a 2 dimesional [3][22] array passed in as a parameter
 * 		[0] - directory
 * 		[1] - file name
 * 		[2] - extension
 *------------------------------------------------------------------------------------------------------------------------------------------

Return Codes:
(path contains...)
1 empty
2 directory only
3 directory and file

Error Codes:
0  path is null
-1 path name was too long
-2 the directory name was too long
-3 the file name was too long
-4 the extension was too long
-5 the path didn't even include a directory
-6 either the file or the extension is missing
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
static int parsePath(const char *path, char parsed [3][22])
{
	//set the strings to be empty
	parsed[0][0] = '\0';
	parsed[1][0] = '\0';
	parsed[2][0] = '\0';
	
	if(!path) return 0;//the path is null
	if(strlen(path) > 22) return -1;//the path is too long
	if(strlen(path) == 1) return 1; //the path is empty (contains only '\')
	
	sscanf(path, "/%[^/]/%[^.].%s", parsed[0], parsed[1], parsed[2]);

	if(strlen(((char*)parsed[0])) > 8) return -2;//the directory name is too long
	if(strlen(((char*)parsed[1])) > MAX_FILENAME) return -3;//the file name is too long
	if(strlen(((char*)parsed[2])) > MAX_EXTENSION) return -4;//the extension is too long
	if(strlen(((char*)parsed[0])) == 0) return -5;//no directory specified
	if(strlen(((char*)parsed[1])) == 0) return 2;//only a directory was specified
	if(strlen(((char*)parsed[1]))!= 0 && strlen(((char*)parsed[2])) != 0) return 3;//both a file and extension were specified
	return -6;//either the file or the extension is missing
}



/*------------------------------------------------------------------------------------------------------------------------------------------
 * getDirectories
 * 		sets the parameter to point to the .directories binary file		
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
FILE * getDirectories()
{	
	//open the file for reading and writing
	FILE * dir = fopen(".directories", "r+b");
	
	//if the open was succesfull, return it
	if(dir != 0) return dir;
	
	//otherwise, create a file
	dir = fopen(".directories", "w+b");
	

	return dir;
}

/*-----------------------------------------------------------------------------------------------------------------------
 * doesDirExist
 * 		return true or false if the dir exists
 * 		-ignores 'deleted' directories
 *------------------------------------------------------------------------------------------------------------------------------------------
return code:
-1 not found
>-1 found (index)
*/
int doesDirExist(const char *path)
{
	cs1550_directory_entry entry;
	int index = 0;
	
	//loop thorugh each dir index in the file 
	for(getDirAt(index,&entry); getDirAt(index,&entry) != -1; index++)
	{
		//skip any deleted file
		if(entry.nFiles == -1) continue;		
		//if the directory is found, return the index
		if(strcmp(path,entry.dname) == 0) return index;
	}
	
	return -1;
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * getDirAt
 * 		get the dir at a given index
 *------------------------------------------------------------------------------------------------------------------------------------------
Return Code
1 success

Error code:
-1 seek to index was unsuccesful (out of bounds)
-2 the read to the index has failed (the entry is 1 index out of bounds)
-3 .directories failure
*/
int getDirAt(int index, cs1550_directory_entry* entry){
	int retcode = 0;
	FILE * dir = getDirectories();

	//if we failed to open/create the directory, no reason to continue
	if(!dir) return -3; 
	
	
	//seek to the index
	retcode = fseek(dir, index*sizeof(cs1550_directory_entry), SEEK_SET);
	//if the seek was unsuccesfull, return failure	

	//get the entry
	retcode = fread(entry,sizeof(cs1550_directory_entry),1,dir);
	//if we have not recieved an entry, return failure
	if(retcode != 1)
	{
		fclose(dir);
		return -1;
	}
	
	fclose(dir);
	return 1;
}


/*------------------------------------------------------------------------------------------------------------------------------------------
 * writeDir
 * 		write the directory to either an open spot or append it
 *------------------------------------------------------------------------------------------------------------------------------------------*/
int writeDir(cs1550_directory_entry* entry){
	int index = 0;
	cs1550_directory_entry directory;
	
	//search thorugh each direcotry
	for(getDirAt(index,&directory); getDirAt(index,&directory) != -1; index++)
	{
		//if the current directory is invalid (nFiles == -1) overwrite it
		if(directory.nFiles == -1){
			writeDirAt(index, entry);
			return index;
		}
	}
	
	//if no free directory was found, append the desired directory
	writeDirAt(index, entry);
	return index;
}
/*------------------------------------------------------------------------------------------------------------------------------------------
 * writeDirAt
 * 		write the directory to the directory index into the file
 *------------------------------------------------------------------------------------------------------------------------------------------*/
int writeDirAt(int index, cs1550_directory_entry* entry){
	FILE * dir = getDirectories();
		
	//seek to the directory
	int retcode = fseek(dir, index*sizeof(cs1550_directory_entry), SEEK_SET);

	//write to that location
	int sizeCheck = fwrite(entry, sizeof(cs1550_directory_entry), 1, dir);

	fclose(dir);
	
	//if the write failed, return failure
	if(sizeCheck != 1)return -1;
	return 0;
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * getFileindex
 * 		retrieve the file if it exists in the directory
 *------------------------------------------------------------------------------------------------------------------------------------------
Reaturn values:
>=0 - file found and set (to the returned index)
-1 - file not in directory
*/
int getFileIndex(cs1550_directory_entry dir, char filename [MAX_FILENAME+1], char ext[MAX_EXTENSION+1]){
	int fcount = 0;
	//loop until the file is found
	for(fcount = 0; fcount < dir.nFiles; fcount++)
	{
		//skip deleted files
		if(dir.files[fcount].fsize==-1) continue;
		//if both the file name and extension match
		if(strcmp(filename, dir.files[fcount].fname) == 0 && strcmp(ext, dir.files[fcount].fext) == 0)	return fcount; //return the index
	}
	//no equivalent file was found, return failure
	return -1;
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * setBit
 *         calls the appropriat set bit
 *------------------------------------------------------------------------------------------------------------------------------------------*/
int setBit(int onezero, int index)
{
    int byteindex = 0;
    int bitindex = 0;
    unsigned char byte = 0;
    byteindex = index/8;
    bitindex = index%8;

    FILE * disk = fopen(".disk", "r+b");
    if(disk == NULL){
        return -1;
    }

    //move the file pointer to the start of the byte in the bitmap
    fseek(disk,-BYTES_IN_MAP,SEEK_END);
    fseek(disk,byteindex,SEEK_CUR);
    

    fread(&byte, 1, 1, disk);
    
    //call the appropriate function
    if(onezero)
        byte |= 1<<index; 
    else
        byte &= ~(1<<index); 
    
    fseek(disk,-1,SEEK_CUR);
    fwrite(&byte, 1, 1, disk);
    fclose(disk);
    return 1;
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * getBit
 * 		returns the bit associated with the block index
 *------------------------------------------------------------------------------------------------------------------------------------------*/
int getBit(int index){

    FILE * disk = fopen(".disk", "r+b");
    int byteindex = 0;
    int shiftnum = 0;
    unsigned char byte;


    //find the offset
    byteindex = index/8;
    shiftnum = index%8;


    //move the file pointer to the start of the byte in the bitmap
    fseek(disk,-BYTES_IN_MAP,SEEK_END);
    fseek(disk,byteindex,SEEK_CUR);
    
    //read the byte
    fread(&byte, 1, 1, disk);
    
    fclose(disk);
    if((byte & (1<<shiftnum)) == 0){
        return 0;
    }
    else{
        return 1;
    }
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * getFreeBlocks
 * 		returns the block index of the requested space size
 *------------------------------------------------------------------------------------------------------------------------------------------
Return code
>=0 start index of contiguous free blocks of the request size
-1 no large enough block exists
*/
long getFreeBlocks(long request)
{
	FILE * disk = fopen(".disk", "r+b");
	long mapindex = 0;
	long freeblockcount = 0;
	long freestart = -1;
	unsigned char byte;
	
	//move the file pointer to the start of the byte in the bitmap
	fseek(disk,-BYTES_IN_MAP,SEEK_END);
	fclose(disk);
	//loop through bitmap
	for(mapindex = 0; mapindex < (BYTES_IN_MAP*8); mapindex++)
	{	
		//if the bit is free
		if(getBit(mapindex) == 0)	
		{
			//if we haven't found any free contiguous bytes
			if(freestart == -1)
			{
				//set the start of contiguous space to the current map index, and set the count to 0
				freestart = mapindex;
				freeblockcount = 0;
			}
			//if were in the middle of a free byte
			else
			{
				//increment the count of contiguous free blocks
				freeblockcount++;
				
				//if we have reached our request, return the start index
				if(freeblockcount == request)
					return freestart;
			}
		}
		else{
			freestart = -1;
			freeblockcount = 0;
		}		
	}
	
	return -1;	
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * writeToDisk()
 * 		writes the buffer of blocks to the file and marks the appropriate bit in the bitmap
 *------------------------------------------------------------------------------------------------------------------------------------------*/
int writeToDisk(int block,  off_t offset, const char * buffer, size_t size)
{
	int mapindex = 0;
	int numblocks = 0;
	FILE * disk = fopen(".disk","r+b");
	
	//make sure we aren't trying to overwrite our bitmap
	if((DISK_SIZE-(BLOCK_SIZE*block)+offset+size) < (BYTES_IN_MAP)) return -1;
	
	//seek to the start of the block being written to, then move to the offset
	fseek(disk,(BLOCK_SIZE*block)+offset,SEEK_SET);
	
	//write the blocks to the the target location
	fwrite(buffer, 1, size, disk);	
	
	//move to the block at containing the start offset
	mapindex = block+(offset/BLOCK_SIZE);
	
	//get the number of blocks rounded down, offset by the write location
	numblocks = mapindex+(size/BLOCK_SIZE);
	
	//if there is leftover memory, allocate an addtional block
	if((size-(numblocks*BLOCK_SIZE)) > 0) numblocks++;
	
	//set the blocks written to to be occupied
	for(mapindex = mapindex; mapindex < numblocks; mapindex++)	setBit(1, mapindex);

	fclose(disk);
	return 1;
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * readFromDisk()
 * 		reads the buffer of blocks to the file 
 *------------------------------------------------------------------------------------------------------------------------------------------*/
int readFromDisk(int block,  off_t offset, char * buffer, size_t size)
{
	FILE * disk = fopen(".disk","r+b");
	
	//seek to the start of the block being written to, then move to the offset
	fseek(disk,(BLOCK_SIZE*block)+offset,SEEK_SET);
	
	//write the blocks to the the target location
	fread(buffer, 1, size, disk);	
	
	fclose(disk);
	return 1;
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * addFileToDir()
 * 		adds the file to the directory, starting by overwriting unused files
 *------------------------------------------------------------------------------------------------------------------------------------------
Retrun code
1 add succesfull

Error Code
-1 directory full
*/
int addFileToDir(cs1550_directory_entry  *dir, char fname[MAX_FILENAME + 1], char  fext[MAX_EXTENSION + 1],size_t fsize,long nStartBlock)
{
	int index;
	
	//the directory is full, return failure
	if(dir->nFiles == MAX_FILES_IN_DIR) return -1;
	
	//loop through all files
	for(index = 0; index < MAX_FILES_IN_DIR; index++){
		
		//if we find an open location, write to it
		if(dir->files[index].fsize == -1){
			strcpy(dir->files[index].fname, fname);
			strcpy(dir->files[index].fext, fext);
			dir->files[index].fsize = fsize;
			dir->files[index].nStartBlock = nStartBlock;
			dir->nFiles++;
			return 1;
		}
	}
	
	//if we've gotten this far, no files are free
	return -1;
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * growFile()
 * 		given a the direcotry and file index, resize the file to overlap the size desired
 * 		*pass in the total file size desired in bytes*
 *------------------------------------------------------------------------------------------------------------------------------------------
Return code
1 success

Error code
-1 not enough free space on disk
*/
int growFile(cs1550_directory_entry * dir, int fileindex, size_t newsize)
{
	size_t fsize = 0;
	int fnumblock;
	int startblock = -1;
	int additional = 0;
	int endblock = 0;
	int blockindex = 0;
	int relocate = 0;
	char * buffer;
	
	fsize = dir->files[fileindex].fsize;
	startblock = dir->files[fileindex].nStartBlock;
	
	//find the last block in the file
	endblock = startblock + (fsize/BLOCK_SIZE);
	
	//get the size of the file in blocks
	fnumblock = fsize/BLOCK_SIZE;
	
	//find how many additional blocks we need
	additional = ((newsize + (newsize%BLOCK_SIZE))/BLOCK_SIZE) - (fnumblock);
	
	//look through the desired blocks to write to, and see if we have adequit space
	for(blockindex = endblock; blockindex < (endblock+additional); blockindex++)
	{
		//if the block is used, mark that we need to relocate
		if(getBit(blockindex) == 1) relocate = 1;
	}
	
	//if the file needs to be relocated
	if(relocate == 1){
		//look for a new location with enough contiguous free blocks
		relocate = getFreeBlocks((newsize + (newsize%BLOCK_SIZE))/BLOCK_SIZE);
		
		//no large enough block exists
		if(relocate <0) return -1;
		
		//allocate space for the file transfer
		buffer = malloc(newsize);
		
		//read the file into the buffer
		readFromDisk(startblock, 0, buffer, fsize);
		
		//copy the data to the new location (this also sets the bitmap values)
		writeToDisk(relocate, 0, buffer, newsize);
		
		//free all of the blocks
		for(blockindex = startblock; blockindex < endblock; blockindex++)	setBit(0, blockindex);
		
		//set the new properties of the file
		dir->files[fileindex].fsize = newsize;
		dir->files[fileindex].nStartBlock = relocate;
		
		free(buffer);
	}
	//otherwise, just mark the next block(s) as used
	else{
		//create an appropriately sized buffer to expand the file
		buffer = malloc(newsize - fsize);
		
		//write empty chars to the file to set the blocks as used
		writeToDisk(endblock+1, 0, buffer, newsize - fsize);
		
		free(buffer);
	}
	return 0;
}


/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{	
	//retrieve the path
	char parsedpath[3][22];
	int retcode;
	int res = 0;
	int index = 0;
	cs1550_directory_entry entry;

	retcode = parsePath(path, parsedpath);		

	memset(stbuf, 0, sizeof(struct stat));
   	
	//if this is the root dir, return normal file permission
	if (strcmp(path, "/") == 0) 
	{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;		
	} 
	
	//if it is a subdirectory, check if it exists, and retun its permission
	else if(retcode == 2)
	{
		//if the directory exists, return it to have standard attributes
		if(doesDirExist(parsedpath[0]) != -1){
			stbuf->st_mode = S_IFDIR | 0755;
			stbuf->st_nlink = 2;
		}
		else{
			res = -ENOENT;
		}
	}
	//if its a file
	else if(retcode == 3)
	{		
		//the directory must exist if we've gotten this far, find it
		for(getDirAt(index, &entry); getDirAt(index, &entry) >=0; index++) 
		{
			//if the directory is found
			if(strcmp(parsedpath[0],entry.dname) == 0) break;
		}
		
		
		if(strcmp(parsedpath[0],entry.dname) != 0)	return -ENOENT;

			
		//get the index of the file in the directory file array
		index = getFileIndex(entry, parsedpath[1], parsedpath[2]);

		//retireve the file, if it doesn't exist, return failure
		if(index<0) return -ENOENT;
		
		//regular file, probably want to be read and write
		stbuf->st_mode = S_IFREG | 0666; 
		stbuf->st_nlink = 1; //file links
		stbuf->st_size = entry.files[index].fsize; //file size - make sure you replace with real size!

		res = 0; // no error
	}
	
	else
	{
		//Else return that path doesn't exist
		res = -ENOENT;
	}
	return res;
}


/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	cs1550_directory_entry entry;
	int index = 0;
	//retrieve the path
	char parsedpath [3][22];
	int retcode = parsePath(path, parsedpath);
	//char * filename;
	char * filename[13];
	
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;
	
	
	
	//if it is just the root directory
	if (strcmp(path, "/") == 0)
	{	
		//the filler function allows us to add entries to the listing
		//read the fuse.h file for a description (in the ../include dir)
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		
		//print all sub directories
		for(getDirAt(index, &entry); getDirAt(index, &entry) >=0; index++) 
		{
			if(entry.nFiles == -1) continue;
			filler(buf, (entry.dname), NULL, 0);
		}
		return 0;
	}
	//if the directory exists
	else if (doesDirExist(parsedpath[0])!=-1)
	{
		//if the return code was negative, something is wrong with the path, return
		if(retcode < 0) return -ENOENT;
				
		//the directory must exist if we've gotten this far, find it
		for(getDirAt(index, &entry); getDirAt(index, &entry) >=0; index++) 	if(strcmp(parsedpath[0],entry.dname) == 0) break;
		
		//if a file was listed, print only its data
		if(retcode == 3){
			index = getFileIndex(entry, parsedpath[1], parsedpath[2]);
			
			//retrieve the file, if it doesn't exist, return failure
			if(index <= 0) return -ENOENT;
			
			strcat(filename,entry.files[index].fname);
			strcat(filename,".");
			strcat(filename,entry.files[index].fext);
			
			//set the attributes
			filler(buf, filename, NULL, 0);						
		}
		//if no file was listed, print the directory contents
		else{
			//set the filler with the file names
			for(index = 0; index < entry.nFiles; index++){
				
				//skip files marked as deleted
				if(entry.files[index].fsize == -1) continue;
				
				//concatinate them together
				strcpy(filename,entry.files[index].fname);
				strcat(filename,".");
				strcat(filename,entry.files[index].fext);
				
				//set the attributes
				filler(buf, filename, NULL, 0);				
			}
		}
	}
	
	else{
	     return -ENOENT;
	}
	
	return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	int index = 0;
	
	//retrieve the path
	char parsedpath [3][22];

	int retval = parsePath(path, parsedpath);

	cs1550_directory_entry entry;
	
	//if the dir name is too long, return it
	if(retval == -2) return -ENAMETOOLONG;
	
	//if any other failure occured, they listed more than just a properly formatted directory
	if(retval != 2) return -EPERM;
	
	//set each files size to -1
	for(index = 0; index <MAX_FILES_IN_DIR; index++) entry.files[index].fsize = -1;
	
	//copy each char to the new dir entry name
	strcpy(entry.dname, parsedpath[0]);
	
	//set the number of files to 0 by default
	entry.nFiles = 0;
	
	writeDir(&entry);
	return 0;
}

/* 
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	//retrieve the path
	char parsedpath [3][22];
	int retval = parsePath(path, parsedpath);
	int index = 0;

	cs1550_directory_entry entry;
	
	//if the path is not a directory, return an error
	if(retval !=2) return -ENOTDIR;
	
	//if the directory is not found, return an error
	if(doesDirExist(parsedpath[0]) == -1)return -ENOENT; 
	
	//loop thorugh each dir index in the file 
	for(getDirAt(index,&entry); getDirAt(index,&entry) != -1; index++)
	{
		//if the directory is found
		if(strcmp(parsedpath[0],entry.dname) == 0){
			if(entry.nFiles > 0) return -ENOTEMPTY;//if it is not empty, return error
			if(entry.nFiles < 0) return -ENOENT;
		
			entry.nFiles = -1;//set the number of files to -1
			writeDirAt(index,&entry);//write the 'erased' directory back
			return 0;
		}
	}
	return -ENOENT;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{	
	(void) mode;
	(void) dev;
	(void) path;
	long blockindex = -1;//store the start block of our file
    char parsed [3][22];
	char buffer[BLOCK_SIZE];
	cs1550_directory_entry dir;
    int dirindex; //holds the index of the directory in the file
    int index = 0;
    size_t fsize = BLOCK_SIZE; //holds the size of the file (one block to start with)
    int retcode = 0;

    //parse the path
    retcode = parsePath(path, parsed);
    
    //if the extension of file name was too long, return failure
    if(retcode == -3 || retcode == -4) return -ENAMETOOLONG;
    
    //if the directory is the root directory, return failure
    if(retcode < 0) return -EPERM;
    
    //get the directory
    dirindex = doesDirExist(parsed[0]);    
    retcode = getDirAt(dirindex,&dir);
    
	//something has gone wrong, return failure
	if(retcode == -1) return 0;
	
	//check that the file doesn't already exist
	if(getFileIndex(dir,parsed[1], parsed[2])>= 0) return -EEXIST;

	//request one block by default
	blockindex = getFreeBlocks(1);

	//no block was found, return 0
	if(blockindex <0) return 0;

    //write to the directory
    retcode = addFileToDir(&dir, parsed[1], parsed[2], fsize, blockindex);
	
	//if the directory is full, return
	if(retcode == -1)return 0;
	
	//mark the appropriate bitmap block with zeroes
	retcode = writeToDisk(blockindex, 0, buffer, 1);

	//if the write to disk fails, do not write the directory back
	if(retcode == -1) return 0;	
	
	//write the directory back to disk
	writeDirAt(dirindex, &dir);
	
	return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;
    cs1550_directory_entry dir;
    char parsed [3][22];
    int dirindex; //holds the index of the directory in the file
    int fileindex; //holds the location of the file in the directory array
    long start; //holds the starting block of the file
    size_t size = 0; //holds the size of the file
    int retcode = 0;
    
    //parse the path
    parsePath(path, parsed);
    
    //get the directory
    dirindex = doesDirExist(parsed[0]);    
    retcode = getDirAt(dirindex,&dir);
    
    //if the directory was not retrieved, return
    if(retcode < 0) return 0;
    
    //retrieve the index of the file in the directories array
    fileindex = getFileIndex(dir, parsed[1], parsed[2]);
    
    //if the file was not found, return failure
    if(fileindex == -1) return 0;
    
    //get the file data
    start = dir.files[fileindex].nStartBlock;
    size = dir.files[fileindex].fsize;
    
    //set the file as deleted
    dir.files[fileindex].fsize = -1;
    
    //decrement the file dount
    dir.nFiles--;
    
    //write the directory back to the file
    writeDirAt(dirindex,&dir);
  
    //find the number of blocks in the file
    size = size/BLOCK_SIZE;
    
    //free the blocks
    for(fileindex = fileindex; fileindex < size; fileindex++) setBit(0, fileindex); //set each block to free
  
    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

    (void) path;
    cs1550_directory_entry dir;
    char parsed [3][22];
    int dirindex; //holds the index of the directory in the file
    int fileindex; //holds the location of the file in the directory array
    int fstart; //holds the starting block of the file
    size_t fsize = 0; //holds the size of the file
    int retcode = 0;
    
    //parse the path
    parsePath(path, parsed);
    
    //if only the directory was listed, return failure
    if(retcode == 2) return EISDIR;
    
    //get the directory
    dirindex = doesDirExist(parsed[0]);    
    retcode = getDirAt(dirindex,&dir);
    
    if(retcode < 0) return ENOENT;
    
    //retrieve the index of the file in the directories array
    fileindex = getFileIndex(dir, parsed[1], parsed[2]);
    
    //if the file was not found, return failure
    if(fileindex == -1) return ENOENT;
    
    //get the file data
    fstart = dir.files[fileindex].nStartBlock;
    fsize = dir.files[fileindex].fsize;
    
    if(size<=0) return 0;
    
    //if the read will exceed the file bounds, return failure
    //should be another error?
    if(offset > fsize || offset < 0) return EFBIG;
    
    //if the read requst is too large, cut it to only read to the end of the file
    if((fsize - offset) < size) size = (fsize - offset);
    
    readFromDisk(fstart, offset, buf, size);

	return size;
}

/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
			  off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

    cs1550_directory_entry dir;
    char parsed [3][22];
    int dirindex; //holds the index of the directory in the file
    int fileindex; //holds the location of the file in the directory array
    long fstart; //holds the starting block of the file
    size_t fsize = 0; //holds the size of the file
    int retcode = 0;
    
    //parse the path
    parsePath(path, parsed);
    
    //get the directory
    dirindex = doesDirExist(parsed[0]);    
    retcode = getDirAt(dirindex,&dir);

    //if the directory was not retrieved, return
    if(retcode < 0) return ENOENT;
    
    //retrieve the index of the file in the directories array
    fileindex = getFileIndex(dir, parsed[1], parsed[2]);
    
    //if the file was not found, return failure
    if(fileindex == -1) return ENOENT;
    
    //get the file data
    fstart = dir.files[fileindex].nStartBlock;
    fsize = dir.files[fileindex].fsize;
    
    if(size<=0) return 0;
    
    if(offset > fsize || offset < 0) return EFBIG;
    
    //if the write will overrun the file size, make space for it
    if(offset+size > fsize)	growFile(&dir, fileindex, (offset+size));
    
    //write the data
    writeToDisk(dir.files[fileindex].nStartBlock, offset, buf, size); 
	return size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
