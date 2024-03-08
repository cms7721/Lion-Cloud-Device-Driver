////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_cache.c
//  Description    : This is the cache implementation for the LionCloud
//                   assignment for CMPSC311.
//
//   Author        : Cole Schutzman
//   Last Modified : 4/19/20
//

// Includes 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <cmpsc311_log.h>
#include <lcloud_cache.h>
#include "lcloud_support.h"

// Functions
int getLine(LcDeviceId did, uint16_t sec, uint16_t blk);

//Structs
typedef struct CACHE_LINE {
    char data[256];
    uint64_t lastUsed;
    LcDeviceId device;
    uint16_t sec;
    uint16_t block;
} CACHE_LINE;

//Global variables
int i;                          //Used for loops, declared now for convienience
CACHE_LINE *cache;              //The cache
uint64_t hits = 0;              //Number of cache hits
uint64_t misses = 0;            //Number of cache misses
int numLines = 0;               //Number of blocks stored in cache
int maxBlocks;
CACHE_LINE *cache;

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_getcache
// Description  : Search the cache for a block 
//
// Inputs       : did - device number of block to find
//                sec - sector number of block to find
//                blk - block number of block to find
// Outputs      : cache block if found (pointer), NULL if not or failure

char * lcloud_getcache( LcDeviceId did, uint16_t sec, uint16_t blk ) {

    int num = getLine(did, sec, blk);           //Which line in cache, if any
    if (num == -1) {                            //If block is not in cache, return NULL, increment misses
        misses++;
        return (NULL);
    }

    else {                                      //Block in cache, update hits and last used
        cache[num].lastUsed = -1;
        hits++;
        for(int j=0;j<numLines;j++) {
            cache[j].lastUsed++;
        }
    }

    return cache[num].data;
 
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_putcache
// Description  : Put a value in the cache 
//
// Inputs       : did - device number of block to insert
//                sec - sector number of block to insert
//                blk - block number of block to insert
// Outputs      : 0 if succesfully inserted, -1 if failure

int lcloud_putcache( LcDeviceId did, uint16_t sec, uint16_t blk, char *block ) {
    int large = 0;                              //Stores the least recently used vlaue to compare against
    int line = 0;                               //Which line in cache to put the block

    int num = getLine(did, sec, blk);
    //Block is not in cache and cache is not full
    if (num == -1 && numLines < maxBlocks) {
        line = numLines;
        numLines++;
    }
    //Block is not in cache but cache is full, evict least recent block
    else if(num == -1) {
        for(i=0;i<maxBlocks;i++) {
            if(cache[i].lastUsed > large) {
                large = cache[i].lastUsed;
                line = i;
            }
        }
    }
    //Cache in block
    else {
        line = num;
    }

    //Set cache line
    cache[line].device = did;
    cache[line].sec = sec;
    cache[line].block = blk;
    cache[line].lastUsed = 0;
    memcpy(cache[line].data,block,256); 

    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_initcache
// Description  : Initialze the cache by setting up metadata a cache elements.
//
// Inputs       : maxblocks - the max number number of blocks 
// Outputs      : 0 if successful, -1 if failure

int lcloud_initcache( int maxblocks ) {
    cache = (CACHE_LINE *)malloc(sizeof(CACHE_LINE) * maxblocks);
    if(cache == NULL) {
        return -1;
    }
    maxBlocks = maxblocks;

    //Initialize each values to nonsense
    for(i=0;i<maxBlocks;i++) {
        cache[i].lastUsed = -1;
        cache[i].device = -1;
        cache[i].sec = -1;
        cache[i].block = -1;
    }
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_closecache
// Description  : Clean up the cache when program is closing
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int lcloud_closecache( void ) {
    free(cache);

    logMessage(LcDriverLLevel,"NUMBER OF HITS: %"PRIu64,hits/2);            //Divide hits by 2 because getcache is called twice per
    logMessage(LcDriverLLevel,"NUMBER OF MISSES: %"PRIu64,misses);             
    float ratio = (float)(hits/2) / (float)((hits/2)+misses);
    logMessage(LcDriverLLevel,"HIT RATIO: %.2f",ratio);                     //Divide hits by 2 because getcache is called twice per

    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : getLine
// Description  : Finds which line, if any the block is stored
//
// Inputs       : did - The device ID the sector is in, sec - The sector the block is stored in, block - the block the data is tored in
// Outputs      : 0 if successful, -1 if failure
int getLine(LcDeviceId did, uint16_t sec, uint16_t blk) {
    for(i=0;i<numLines;i++) {
        if (cache[i].device == did && cache[i].sec == sec && cache[i].block == blk) {
            return i;
        }
    }

    return -1;
}