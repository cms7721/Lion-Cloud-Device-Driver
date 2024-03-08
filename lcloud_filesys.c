////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_filesys.c
//  Description    : This is the implementation of the Lion Cloud device 
//                   filesystem interfaces.
//
//   Author        : Cole Schutzman
//   Last Modified : 5/1/20
//

// Include files
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "cmpsc311_log.h"
#include <unistd.h>
#include <assert.h>
#include "lcloud_cache.h"



// Project include files
#include "lcloud_filesys.h"
#include "lcloud_controller.h"
#include "lcloud_network.h"
#include "lcloud_support.h"


//typedefs and structs

typedef struct MEMORY_ENTRY {       //Used to keep track of where each byte is in the device
    uint32_t startByte;
    uint16_t length;
    uint8_t sec;
    uint8_t block;
    LcDeviceId device;
} MEMORY_ENTRY;

typedef struct FILE_INFO {          //General file info
    LcFHandle handle;
    uint32_t length;
    uint32_t loc;
} FILE_INFO;

typedef struct FILE_OBJ {           //File object
    FILE_INFO info;
    MEMORY_ENTRY *pos; 
    uint32_t entries;
} FILE_OBJ;

typedef struct BLOCK {             //Block object
    LcFHandle handle;
    uint16_t spaceUsed;
} BLOCK;

typedef BLOCK* SECTOR;             //Sector object

typedef struct DEVICE_OBJ {        //Device object
    LcDeviceId id;
    uint16_t numSectors;
    uint16_t numBlocks;
    SECTOR *table;
} DEVICE_OBJ;


//Global Variables
FILE_OBJ files[256];                  //Contains info for each file
uint32_t numHandles = 0;              //Number of open files
DEVICE_OBJ devices[16];              //Device IDs
int numDevices = 0;                  //Number of devices
int on = 0;                          //Power state
int i;                               //Used in for loops, declared now for convienience
//Registers
uint8_t b0;
uint8_t b1;
uint8_t c0;
uint8_t c1;
uint8_t c2;
uint16_t d0;
uint16_t d1;


// File system interface prototypes in header

//Help functions
int checkHandle(LcFHandle h);   //used to match handle to file

int checkId(LcDeviceId d);      //Used to match id to device

int powerOn();                  //Powers bus on the system

int convertId(uint16_t mask);   //Converts device id from mask;

int availableSpace(LcDeviceId device, LcFHandle fh, uint8_t *sec, uint16_t *block); //Checks which block to write to

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcopen
// Description  : Open the file for for reading and writing
//
// Inputs       : path - the path/filename of the file to be read
// Outputs      : file handle if successful test, -1 if failure


LcFHandle lcopen( const char *path ) {

    if(!on) {
        powerOn();
    }

    LcFHandle handle = numHandles;      //Make Handle
    if(checkHandle(handle) != -1) {     //Fail if file already opened
        return -1;
    }

    //Create new file object
    FILE_OBJ fl;
    fl.info.handle = handle;
    fl.info.loc = 0;
    fl.info.length = 0; 
    fl.entries = 0;
    fl.pos = malloc(0);
    //Add to list of open files
    files[numHandles] = fl; 
    numHandles++;

    return handle;
} 


////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcread
// Description  : Read data from the file 
//
// Inputs       : fh - file handle for the file to read from
//                buf - place to put the data
//                len - the length of the read
// Outputs      : number of bytes read, -1 if failure

int lcread( LcFHandle fh, char *buf, size_t len ) {
    uint8_t sec = 0;                                //Sector to read from
    uint16_t block = 0;                             //Block to read from
    DEVICE_OBJ dev;                                 //Device object
    size_t subLen;                                  //How much of the read to do (if read spans multiple blocks)
    char *subBuf  = (char *)malloc(LC_DEVICE_BLOCK_SIZE); //Temporary holder
    int subPos = 0;                                 //How far along read
    FILE_OBJ *fl;                                   //File to read from
    int fIndex;                                     //Which file in array of files
    int off;                                        //Where in memory entry the read starts
    int memPos;

    //Ensure the handle exist, then get the file object
    fIndex = checkHandle(fh);                        
    if(fIndex == -1) {
        return -1;
    }
    fl = &files[fIndex];

    //If the length of read goes past the end of the file, make it go to end of file
    if(len >= fl->info.length - fl->info.loc) {
        len = fl->info.length - fl->info.loc;
    }

    //Keep reading until read completes
    while (subPos < len) {
        //Where in block the file position is in
        off = fl->info.loc%LC_DEVICE_BLOCK_SIZE;

        //Find which memory entry the file position is in
        for(memPos=0;memPos<fl->entries;memPos++) {
            if (fl->info.loc >= fl->pos[memPos].startByte && fl->info.loc < fl->pos[memPos].startByte + fl->pos[memPos].length) {
                sec = fl->pos[memPos].sec;
                block = fl->pos[memPos].block;
                dev = devices[checkId(fl->pos[memPos].device)];
                break;
            }
        }

        //Determine if read can be done in one go
        if ((fl->pos[memPos].length + fl->pos[memPos].startByte) - fl->info.loc >= len - subPos) {
            subLen = len-subPos;
            }
        else {
            subLen = fl->pos[memPos].length - off;
        }

        //read from block and copy the necessary chunk to buf
        if(lcloud_getcache(dev.id,sec,block) == NULL) {
            client_lcloud_bus_request(create_lcloud_registers(0,0,LC_BLOCK_XFER,dev.id,LC_XFER_READ,sec,block),subBuf);
        }
        else {
            memcpy(subBuf,lcloud_getcache(dev.id,sec,block),LC_DEVICE_BLOCK_SIZE);
        }

        memcpy(&buf[subPos],&subBuf[(off)],subLen);

        //file tracking
        subPos += subLen; 
        fl->info.loc += subLen;
    }

    //cleanup
    free(subBuf);
    subBuf = NULL;

    return( len );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcwrite
// Description  : write data to the file
//
// Inputs       : fh - file handle for the file to write to
//                buf - pointer to data to write
//                len - the length of the write
// Outputs      : number of bytes written if successful test, -1 if failure
int lcwrite( LcFHandle fh, char *buf, size_t len ) {
    uint8_t sec = 0;                                //Sector to write to
    uint16_t block = 0;                            //block to write to
    size_t subLen;                                  //How much to write for this pass
    char *subBuf = (char *)malloc(LC_DEVICE_BLOCK_SIZE); //Temporary holder
    int subPos = 0;                                 //How far along current write
    FILE_OBJ *fl;                                   //File to write to
    int fIndex;                                     //Which file in array of files
    int memPos = -1;
    int overwrite = 0;
    DEVICE_OBJ *dev;

    //Ensure the handle exist, then get the file object
    fIndex = checkHandle(fh);
    if(fIndex == -1) {
        return -1;
    }
    fl = &files[fIndex];

    //Check if overwriting old data
    if(fl->info.loc < fl->info.length) {
        overwrite = 1;
    }

    //increase file length if necessary
    if(fl->info.loc + len > fl->info.length) {
        fl->info.length += len - (fl->info.length - fl->info.loc);
    }

    //Keep writing until write is complete
    while (subPos < len) {

        //Find next available block to write to
        if(!overwrite) {
            for(int q=0;q<numDevices;q++) {
                if(availableSpace(devices[q].id,fh,&sec,&block) == 0) {
                    dev = &devices[q];
                    break;
                }
            }
        }
        //Find which block to overwrite if overwriting
        else {
            for(memPos=0;memPos<fl->entries;memPos++) {
                if (fl->info.loc >= fl->pos[memPos].startByte && fl->info.loc < fl->pos[memPos].startByte + fl->pos[memPos].length) {
                    sec = fl->pos[memPos].sec;
                    block = fl->pos[memPos].block;
                    dev = &devices[checkId(fl->pos[memPos].device)];
                    break;
                }
            }
        }

        //Get whats already in block to prevent unintentional overwritting
        
        if(lcloud_getcache(dev->id,sec,block) == NULL) {
            client_lcloud_bus_request(create_lcloud_registers(0,0,LC_BLOCK_XFER,dev->id,LC_XFER_READ,sec,block),subBuf);
        }
        else {
            memcpy(subBuf,lcloud_getcache(dev->id,sec,block),LC_DEVICE_BLOCK_SIZE);
        }

        //Check if write can be done in one pass
        if(len-subPos <= LC_DEVICE_BLOCK_SIZE - dev->table[sec][block].spaceUsed) {
            subLen = len-subPos;
        }  
        else if(overwrite==1) {
            if(len - subPos >= (fl->pos[memPos].startByte + fl->pos[memPos].length) - fl->info.loc) {
                subLen = LC_DEVICE_BLOCK_SIZE - fl->info.loc%LC_DEVICE_BLOCK_SIZE;
            }
            else{
                subLen = len - subPos;
            }
        }       
        else {
            subLen = LC_DEVICE_BLOCK_SIZE - dev->table[sec][block].spaceUsed;
        }
        
        //Insert write into block
        memcpy(&subBuf[fl->info.loc%LC_DEVICE_BLOCK_SIZE],&buf[subPos],subLen);
        client_lcloud_bus_request(create_lcloud_registers(0,0,LC_BLOCK_XFER,dev->id,LC_XFER_WRITE,sec,block),subBuf);
        lcloud_putcache(dev->id, sec, block, subBuf);

        //House keeping
        dev->table[sec][block].handle = fh;
        subPos += subLen;

        //If overwriting and write goes beyond end of memoryEntry, increase file length and alter memory entry
        if(overwrite && subLen + (fl->info.loc - fl->pos[memPos].startByte) > fl->pos[memPos].length) {
            fl->pos[memPos].length = subLen + (fl->info.loc - fl->pos[memPos].startByte);
            dev->table[sec][block].spaceUsed = fl->pos[memPos].length;
            assert(fl->pos[memPos].length<=256);
            assert(dev->table[sec][block].spaceUsed <=256);
        }

        //Otherwise if overwriting, do nothing
        else if(overwrite) {
            assert(dev->table[sec][block].spaceUsed <= 256);
        }

        //If this is not the first write, but not overwriting, increase file length, alter memory entry
        else if(fl->entries && fl->pos[fl->entries-1].startByte + fl->pos[fl->entries-1].length == fl->info.loc && 
            fl->pos[fl->entries-1].sec == sec && fl->pos[fl->entries-1].block == block && fl->pos[fl->entries-1].device == dev->id) {
            fl->pos[fl->entries-1].length += subLen;
            if(fl->pos[fl->entries-1].length > 256){
            assert(fl->pos[fl->entries-1].length <= 256);}
            dev->table[sec][block].spaceUsed += subLen;
            assert(dev->table[sec][block].spaceUsed <= 256);
        }

        //Make new memory entry
        else {
            fl->pos = (MEMORY_ENTRY *)realloc(fl->pos,sizeof(MEMORY_ENTRY) * (fl->entries+1));
            fl->pos[fl->entries].startByte = fl->info.loc;
            fl->pos[fl->entries].length = subLen;
            fl->pos[fl->entries].sec = sec;
            fl->pos[fl->entries].block = block;
            fl->pos[fl->entries].device = dev->id;
            fl->entries++;
            dev->table[sec][block].spaceUsed += subLen;
            assert(fl->pos[fl->entries-1].length <= 256);
            assert(dev->table[sec][block].spaceUsed <= 256);
        }

        //Set file position
        fl->info.loc += subLen;
    }

    //Cleanup
    free(subBuf);
    subBuf = NULL;

    return( len );
}




////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcseek
// Description  : Seek to a specific place in the file
//
// Inputs       : fh - the file handle of the file to seek in
//                off - offset within the file to seek to
// Outputs      : 0 if successful test, -1 if failure

int lcseek( LcFHandle fh, size_t off ) {

    //Ensure the handle exist, then get the file object
    int fIndex = checkHandle(fh);
    if(fIndex == -1) {
        return -1;
    }

    //Set file
    FILE_OBJ *fl = &files[fIndex];

    //Fail if offset is bigger than length
    if(off > fl->info.length) {
        return -1;
    }

    //Update position
    fl->info.loc = off;

    return( off );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcclose
// Description  : Close the file
//
// Inputs       : fh - the file handle of the file to close
// Outputs      : 0 if successful test, -1 if failure

int lcclose( LcFHandle fh ) {   //Optimize honors if need be
    FILE_OBJ *temp = files;

    //Get position in file array, fail if file handle is invalid
    int fIndex = checkHandle(fh);
    if(fIndex==-1) {
        return -1;
    }

    free(temp[i].pos);


    //Copy each file after the one to close to position before
    for(i=fIndex+1;i<numHandles;i++) {
        temp[i-1] = temp[i];
    }

    numHandles--;
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcshutdown
// Description  : Shut down the filesystem
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int lcshutdown( void ) {

    //Send shutdown 
    LCloudRegisterFrame frame = create_lcloud_registers(0,0,LC_POWER_OFF,0,0,0,0);
    if(extract_lcloud_registers(client_lcloud_bus_request(frame, NULL), &b0,&b1,&c0,&c1,&c2,&d0,&d1)==-1 || b1 != 1) {
        return -1;
    }

    //Close all files
    for(i=0;i<numHandles;i++) {
        lcclose(files[i].info.handle);
    }

    lcloud_closecache();

    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : create_lcloud_registers
// Description  : Creates a 64 bit number instruction out of registers
//
// Inputs       : b0 - the B0 register (send/recieve), b1- the B1 register (0), c0 - the C0 register (opcode), c1 - the C1 register, c2 - the C2 register, d0 - the D0 register, d1 - the D1 register
// Outputs      : a 64 bit instruction number
LCloudRegisterFrame create_lcloud_registers(uint8_t b0, uint8_t b1, uint8_t c0, uint8_t c1, uint8_t c2, uint16_t d0, uint16_t d1) {
    LCloudRegisterFrame frame = ((uint64_t)b0<<60) | ((uint64_t)b1<<56) | ((uint64_t)c0<<48) | ((uint64_t)c1<<40) | ((uint64_t)c2<<32) | ((uint64_t)d0<<16) | ((uint64_t)d1<<0);
    return frame;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : extract_lcloud_registers
// Description  : Breaks up response from bus into individual registers
//
// Inputs       : resp - the frame to break up, *b0 - pointer to a variable to store B0, *b1 - pointer to variable to store B1, *b2 - pointer to a variable to store B2
//              *c0 - pointer to variable to store C0, *c1 - pointer tp variable to store C1, *c2, pointer to variable to store C2, *d0 - pointer to variable to store D0
//              *d1, pointer to variable to store D1
// Outputs      : 0 if successful test, -1 if failure
int extract_lcloud_registers(LCloudRegisterFrame resp, uint8_t *b0, uint8_t *b1, uint8_t *c0, uint8_t *c1, uint8_t *c2, uint16_t *d0, uint16_t *d1) {
    *b0 = (resp & 0xF000000000000000) >> 60;
    *b1 = (resp & 0x0F00000000000000) >> 56;
    *c0 = (resp & 0x00FF000000000000) >> 48;
    *c1 = (resp & 0x0000FF0000000000) >> 40;
    *c2 = (resp & 0x000000FF00000000) >> 32;
    *d0 = (resp & 0x00000000FFFF0000) >> 16;
    *d1 = (resp & 0x000000000000FFFF) >> 0;
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : checkHandle
// Description  : findes which file the handle represents
//
// Inputs       : h - the handle to check
// Outputs      : the index, in the files array, of the file, -1 if invalid handle
int checkHandle(LcFHandle h) {
    for(i=0;i<numHandles;i++) {
        if(h == files[i].info.handle) {
            return i;
        }
    }

    return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : checkID
// Description  : findes which device the ID represents
//
// Inputs       : d - the ID to check
// Outputs      : the index, in the files array, of the device, -1 if invalid ID
int checkId(LcDeviceId d) {
    for(i=0;i<numDevices;i++) {
        if(d == devices[i].id) {
            return i;
        }
    }

    return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : powerOn
// Description  : turn on the filesystem, probes device for IDs
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure
int powerOn() {
    LCloudRegisterFrame frame;
    uint16_t idFinder;
    DEVICE_OBJ dev;
    DEVICE_OBJ *devObj;
    i = 0;

    //Power on command
    client_lcloud_bus_request(create_lcloud_registers(0,0,LC_POWER_ON,0,0,0,0),NULL);

    //Device probe
    extract_lcloud_registers(client_lcloud_bus_request(create_lcloud_registers(0,0,LC_DEVPROBE,0,0,0,0),NULL),&b0,&b1,&c0,&c1,&c2,&d0,&d1);
    if (d0 == -1) {
        return -1;
    }
    idFinder = d0;

    while(idFinder != 0 ) {
        if ((idFinder & 0x0001) == 1) {
            dev.id = i;
            devices[numDevices] = dev;
            numDevices++;
        }
        idFinder = idFinder >> 1;
        i++;
    }

    for(i=0;i<numDevices;i++) {
        devObj = &devices[i];
        frame = create_lcloud_registers(0,0,LC_DEVINIT,(uint8_t)(devObj->id),0,0,0);
        extract_lcloud_registers(client_lcloud_bus_request(frame,NULL),&b0,&b1,&c0,&c1,&c2,&d0,&d1);
        devObj->numSectors = d0;
        devObj->numBlocks = d1;
        devObj->table = (BLOCK**)malloc(sizeof(BLOCK*)*d0);
        for(int j=0;j<d0;j++) {
            devObj->table[j] = (BLOCK *)calloc(d1,sizeof(BLOCK));
        }
        for(int j=0;j<d0;j++) {
            for(int k=0;k<d1;k++) {
                devObj->table[j][k].handle = -1;
            }
        }

    } 

    //Initialize Cache
    lcloud_initcache(LC_CACHE_MAXBLOCKS);

    //House keeping
    on = 1;

    return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : convertID
// Description  : Converts recieved mask form probe into actual ID
//
// Inputs       : mask - Mask from device probe
// Outputs      : The device ID, -1 if failure
int convertId(uint16_t mask) {
    i = 0;
    while (mask != 0) {
        if((mask & 0x1) == 1) {
            return i;
        }
        mask = mask >> 1;
        i++;
    }
    return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : availableSpace
// Description  : Finds the proper block to write to
//
// Inputs       : device - the device ID to check, fh - the file handle to check, *sec - pointer to sector variable
//                 *block - pointer to block variable
// Outputs      : 0 if success, -1 if device is full and cant be overwritten
int availableSpace(LcDeviceId device, LcFHandle fh, uint8_t *sec, uint16_t *block) {
    int dIndex;
    DEVICE_OBJ dev;

    dIndex = checkId(device);
    if(dIndex == -1) {
        return -1;
    }
    dev = devices[dIndex];

    for(i=0;i<dev.numSectors;i++) {
        for(int j=0;j<dev.numBlocks;j++) {
            if(dev.table[i][j].spaceUsed < LC_DEVICE_BLOCK_SIZE && (dev.table[i][j].handle == fh || 
            dev.table[i][j].handle == -1)) {
                *sec = i;
                *block = j;
                return 0;
            }
        }
    }

    return -1;

}
