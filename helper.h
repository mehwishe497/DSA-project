#pragma once

#include<string>
#include<iostream>
#include<cstdlib>
#include<cstdint>
#include<cstring>
#ifdef _WIN32
#include<winsock.h>
#pragma comment (lib,"ws2_32.lib")
#define MSG_NOSIGNAL 0
#define SHUT_WR SD_SEND
#define closeSocket closesocket
#else
#include <unistd.h>
#include<sys/socket.h>
#define closeSocket close
#endif
using namespace std;
// Helper.h should contain the structs, functions, constants, etc. that are used more than one time in the project. For example, the structs that are used in both server and client, the constants that are used in all three files. 
const int32_t IO_BUFFER_SIZE=64*1024;
const int LISTEN_BACKLOG=16;
const int MAX_VARS_PER_FRAME=16;
const int MAX_STACK_DEPTH=64;
const char STATUS_OK=0;
const char STATUS_ERROR=1;
struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};
struct TTDBHeader 
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
void startNetwork(){
    #ifdef _WIN32
        WSADATA data;
        WSAStartup(MAKEWORD(2,2),&data);
    #endif
}
bool sendAll(int sock, const char *buf, size_t n) 
{
    // Reply format: [status: 1 byte][sending_material][checksum: 4 bytes].
    // send() may send fewer bytes than asked for, so keep calling it until all n bytes are sent.
     size_t total=0;
     while(total<n){
        int sent=send(sock,buf+total,(int)(n-total),MSG_NOSIGNAL);
        if(sent<=0){
            return false;
        }
        total=total+sent;
     }
     return true;
 }
 
bool recvAll(int sock, char *buf, size_t n)
{
    // recv() can return fewer bytes than asked for, so keep calling it until exactly n bytes are read.
    // Return false if the connection closes or fails before n bytes arrive.
    size_t got=0;
    while(got<n){
        int r=recv(sock,buf+got,(int)(n-got),0);
        if(r<=0){
            return false;
        }
        got=got+r;
    }
     return true;
}
uint32_t crcUpdate(uint32_t crc, const char *data, size_t n){
    for(size_t i=0;i<n;i++){
        crc=crc^(unsigned char )data[i];
        for(int bit=0;bit<8;bit++){
            if(crc&1){
                crc=(crc>>1)^0xEDB88320;

            }
            else{
                crc=crc>>1;
            }
        }

    }
    return crc;
}
uint32_t crcFinish(uint32_t crc){
    return crc^0xFFFFFFFF;
}



class TrailerChecker{
    uint32_t crc;
    string held;
 public:
    TrailerChecker(){
        crc=0xFFFFFFFF;
    }
    string feed(const char *data, size_t n){
        held.append(data,n);
        string body="";
        if(held.size()>4){
            size_t extra= held.size()-4;
            body =held.substr(0,extra);
            crc=crcUpdate(crc,body.data(),extra);
            held.erase(0,extra);
        }
        return body;
    }
    bool verify(){
        if(held.size()!=4){
            return false;
        }
        uint32_t sort;
        memcpy(&sort,held.data(),4);
        return sort==crcFinish(crc);
    }
    uint32_t getCrc(){
        return crcFinish(crc);
    }


};
bool sendError(int sock,const string &message){
    string msg=string(1,STATUS_ERROR)+ message;
    uint32_t crc=crcFinish(crcUpdate(0xFFFFFFFF,msg.data(),msg.size()));
    if(!sendAll(sock,msg.data(),msg.size())){
        return false;
    }
    return sendAll(sock,(const char *)&crc,4);
}
int createListenSocket(int port){
    int sock=socket(AF_INET,SOCK_STREAM,0);
    if(sock<0){
        cout<<"ERROR: cant create socket"<<endl;
        exit(1);
    }
    int yes=1;
    setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(const char *)&yes, sizeof(yes));
    sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(port);
    if(:: bind(sock,(sockaddr *)&addr,sizeof(addr))<0){
        cout<<"ERROR: cant bind to port"<<port<<endl;
        exit(1);
    }    
    if(listen (sock,LISTEN_BACKLOG)<0){
        cout<<"ERROR: listen failed "<<endl;
        exit(1);
    }
    return sock;
}
int connectToHost(const char *host,int port){
    int sock=socket(AF_INET,SOCK_STREAM,0);
    if(sock<0){
       return -1;
    }
    sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=inet_addr(host);
    addr.sin_port=htons(port);

    if(connect(sock,(sockaddr *)&addr,sizeof(addr))<0){
        closeSocket(sock);
        return -1;
    }
    return sock;
}
