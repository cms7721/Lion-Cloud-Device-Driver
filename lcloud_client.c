////////////////////////////////////////////////////////////////////////////////
//
//  File          : lcloud_client.c
//  Description   : This is the client side of the Lion Clound network
//                  communication protocol.
//
//  Author        : Cole Schutzman
//  Last Modified : 5/1/2020
//

// Include Files
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

// Project Include Files
#include <lcloud_network.h>
#include <cmpsc311_log.h>
#include "lcloud_filesys.h"
#include <assert.h>
#include "cmpsc311_util.h"

//Global Variables
int socket_handle = -1;     //Socket
int q;                      //Used in for loops, declared now for convinience
//Registers
uint8_t b0;
uint8_t b1;
uint8_t c0;
uint8_t c1;
uint8_t c2;
uint16_t d0;
uint16_t d1;

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : client_lcloud_bus_request
// Description  : This the client regstateeration that sends a request to the 
//                lion client server.   It will:
//
//                1) if INIT make a connection to the server
//                2) send any request to the server, returning results
//                3) if CLOSE, will close the connection
//
// Inputs       : reg - the request reqisters for the command
//                buf - the block to be read/written from (READ/WRITE)
// Outputs      : the response structure encoded as needed

LCloudRegisterFrame client_lcloud_bus_request( LCloudRegisterFrame reg, void *buf ) {

    char subBuf[1024];                      //Buffer that communicates with network
    LCloudRegisterFrame response;           //Register frame as recieved from network
    LCloudRegisterFrame nReg = htonll64(reg); //Network ordered register frame

    for(q = 0; q < 8; q++) {
        subBuf[q] = ((char *)&nReg)[q];        //Pack frame into buffer
    }

    //Connect to server if not connected
    if (socket_handle == -1) {
        socket_handle = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_handle == -1) {
            assert(0);
        }
        
        struct sockaddr_in caddr;
        caddr.sin_family = AF_INET;
        caddr.sin_port = htons(LCLOUD_DEFAULT_PORT);
        if(inet_aton(LCLOUD_DEFAULT_IP, &caddr.sin_addr) == 0) {
            assert(0);
        }

        if(connect(socket_handle,(const struct sockaddr *)&caddr, sizeof(caddr)) == -1) {
            assert(0);
        }
    }

    //Determine operation and act accordingly
    extract_lcloud_registers(reg, &b0,&b1,&c0,&c1,&c2,&d0,&d1);
    //Block transfer
    if(c0 == LC_BLOCK_XFER) {

        //Block read
        if(c2 == LC_XFER_READ) {
            assert(write(socket_handle, subBuf, 8) != -1);
            assert(read(socket_handle,&subBuf[0],1024) != -1);
            memcpy(buf,&subBuf[8],256);
        }

        //Block write
        else {
            memcpy(&subBuf[8],buf,256);
            assert(write(socket_handle, subBuf, 264) != -1);
            assert(read(socket_handle, subBuf, 1024) != -1);
        }

    } 

    //Shutdown
    else if(c0 == LC_POWER_OFF) {
        assert(write(socket_handle,subBuf,8) != -1);
        assert(read(socket_handle,&subBuf[0],1024) != -1);
        //Close socket
        assert(close(socket_handle) != -1);
        socket_handle = -1;
    }

    //Device Probe
    else if (c0 == LC_DEVPROBE) {
        assert(write(socket_handle,subBuf,8) != -1);
        assert(read(socket_handle,&subBuf[0],1024) != -1);
    }

    //Any other operation
    else {
        assert(write(socket_handle,subBuf,8) != -1);
        read(socket_handle,&subBuf[0],1024);
    }

    //Pack server response into frame. then put into host byte order
    for(q = 0; q < 8; q++) {
        ((char *)&response)[q] = subBuf[q];
    }
    response = ntohll64(response);   

    return response;
}
