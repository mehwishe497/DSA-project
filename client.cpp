// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// #include <unistd.h>
#include <signal.h>
#include <vector>
// #include <sys/socket.h>
#include "helper.h"
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";
// const int IO_BUFFER_SIZE = 64 * 1024;     // fixed buffer for streaming files to and from the socket
const char *TDBG_FILE = "ttdb_last.tdbg"; // where the downloaded .tdbg is saved
const char *TDBG_OTHER = "ttdb_other.tdbg";
const int MAX_JUMP = 1000000000;    // biggest n accepted by jmp
const int HEADER_SIZE = 20;       
string LB_HOST = "127.0.0.1";
int LB_PORT = 5000;

// These two must be the same as in Phase 1

// const int MAX_VARS_PER_FRAME = 16;
// const int MAX_STACK_DEPTH = 64;


// // ---- Structs (same as Phase 1, so the file can be read back exactly as it was written) ----
// struct Variable
// {
//     string name;
//     int32_t value;
// };
// struct Frame
// {
//     string func_name;
//     int32_t argc;
//     Variable argv[MAX_VARS_PER_FRAME];
//     int32_t returnLine;
//     Variable locals[MAX_VARS_PER_FRAME];
//     int32_t localCount;
// };
// struct Snapshot
// {
//     Frame callStack[MAX_STACK_DEPTH];
//     int32_t stackDepth;
// };
// struct TTDBHeader 
// {
//     char magic[4]; // "TTDB"
//     int32_t version;
//     int32_t stepCount;
//     int64_t indexOffset;
// };

// ---- Structs only the client needs ----
struct TdbgReader
{
    FILE *file;
    TTDBHeader header;
    vector<int64_t> index;
};
struct DebugSession
{
    // int active_flag;     // false until a "start" has worked
    bool active;
    TdbgReader reader;   // the .tdbg file stays open for the whole session
    int64_t currentStep; 
    string path;
};

// // =============================================================================================
// // PART A: TALKING TO THE LOAD BALANCER
// // =============================================================================================

// bool recvAll(int sock, char *buf, size_t n)
// {
//     // recv() can return fewer bytes than asked for, so keep calling it until exactly n bytes are read.
//     // Return false if the connection closes or fails before n bytes arrive.
// }
// bool sendAll(int sock, const char *buf, size_t n)
// {
//     // send() can send fewer bytes than asked for, so keep calling it until all n bytes are sent.
//     // Use the flag MSG_NOSIGNAL. Return false if the connection fails.
// }

int connectToHost(const char *host, int port)
{
    // Create a TCP socket and connect it to host:port.
    // Return the socket, or -1 if the connection fails.
}
bool uploadSource(int sock, const char *sourcePath)
{
    // have to do checksum and then send
    // When the file is finished, call shutdown(sock, SHUT_WR). This is how the server knows the string is complete.
    // Return false if the file cannot be opened or any send fails.
    FILE *fi =fopen(sourcePath,"rb");
    if(fi==NULL){
        return false;
    }
     char status = STATUS_OK;
    uint32_t crc = crcUpdate(0xFFFFFFFF, &status, 1);
    if(!sendAll(sock,&status,1)){
        fclose(fi);
        return false;
    }
    vector<char> bufff(IO_BUFFER_SIZE);
    size_t n;
    while((n=fread(&bufff[0],1, IO_BUFFER_SIZE, fi))>0){
        crc=crcUpdate(crc,&bufff[0],n);
        if (!sendAll(sock, &bufff[0], n))
        {
            fclose(fi);
            return false;
        }
    }
    fclose(fi);
    crc=crcFinish(crc);
    if (!sendAll(sock, (const char *)&crc, 4)){
       
       return false;
    }
    shutdown(sock,SHUT_WR);
    return true;

}

bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    // Reply on the wire: [status][payload][CRC-32]. The CRC covers the status byte too.
    // Feed every received byte (status included) to a TrailerChecker.
    // Status 1: collect the payload into outError. Status 0: write the payload to the file.
    // At the end call verify(). If it fails, delete the file, set outError to
    // "checksum mismatch, response corrupted" and return false.
    TrailerChecker check;
    vector<char> bufff(IO_BUFFER_SIZE);
    FILE *out = NULL;
    bool firstByte = true;
    int status = -1;
    string errorText = "";
    bool broken = false;
    while (true)
    {
        int r = recv(sock, &bufff[0], IO_BUFFER_SIZE, 0);
        if (r < 0)
            broken = true;
        if (r <= 0)
            break;
        string body = check.feed(&bufff[0], r);
        if (body.empty())
            continue;
        if (firstByte)
        {
            status = (unsigned char)body[0];
            body.erase(0, 1);
            firstByte = false;
            if (status == STATUS_OK)
            {
                out = fopen(outTdbgPath, "wb");
                if (out == NULL)
                {
                    outError = "cannot create " + string(outTdbgPath);
                    return false;
                }
            }
        }
        if (status == STATUS_ERROR){
            errorText += body;
        }
           
        else if (out != NULL && !body.empty())
          { 
             fwrite(body.data(), 1, body.size(), out);
          }
    }
     if (out != NULL)
        fclose(out);

    if (broken || !check.verify())
    {
        remove(outTdbgPath);
        outError = "checksum mismatch, response has been corrupted";
        return false;
    }
    if (status == STATUS_ERROR)
    {
        outError = errorText;
        return false;
    }
    if (status != STATUS_OK)
    {
        outError = "empty reply from the load balancer";
        return false;
    }
    return true;
}




bool requestDebugFile(const char *sourcePath, const char *outPath, string &outError)
{
    int sock = connectToHost(LB_HOST.c_str(), LB_PORT);
    if (sock < 0)
    {
        outError = "cant connect to load balancer";
        return false;
    }
    if (!uploadSource(sock, sourcePath))
    {
        outError = "could not upload " + string(sourcePath);
        closeSocket(sock);
        return false;
    }
    bool okie = receiveResponse(sock, outPath, outError);
    closeSocket(sock);
    return okie;
}
// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================
bool readInt(FILE *fi, int32_t &v)
{
    return fread(&v, sizeof(int32_t), 1, fi) == 1;
}
bool readStr(FILE *fi, string &s)
{
    int32_t len;
    if (!readInt(fi, len) || len < 0 || len > 1000)
        return false;
    s.resize(len);
    if (len > 0 && fread(&s[0], 1, len, fi) != (size_t)len)
        return false;
    return true;
}
bool readFrame(FILE *fi, Frame &fr)
{
    if (!readStr(fi, fr.func_name) || !readInt(fi, fr.argc))
        return false;
    if (fr.argc < 0 || fr.argc > MAX_VARS_PER_FRAME)
        return false;
    for (int i = 0; i < fr.argc; i++)
    {
        if (!readStr(fi, fr.argv[i].name) || !readInt(fi, fr.argv[i].value))
            return false;
    }
    if (!readInt(fi, fr.returnLine) || !readInt(fi, fr.localCount))
        return false;
    if (fr.localCount < 0 || fr.localCount > MAX_VARS_PER_FRAME)
        return false;
    for (int i = 0; i < fr.localCount; i++)
    {
        if (!readStr(fi, fr.locals[i].name) || !readInt(fi, fr.locals[i].value))
            return false;
    }
    return true;
}
bool openTdbg(const char *path, TdbgReader &r)
{
     r.file = fopen(path, "rb");
    if (r.file == NULL)
    return false;
    if (fread(r.header.magic, 1, 4, r.file) != 4 || fread(&r.header.version, 4, 1, r.file) != 1 ||
    fread(&r.header.stepCount, 4, 1, r.file) != 1 ||fread(&r.header.indexOffset, 8, 1, r.file) != 1){
        fclose(r.file);
        r.file = NULL;
        return false;
    }
    fseek(r.file, 0, SEEK_END);
    long fileSize = ftell(r.file);
    bool good = memcmp(r.header.magic, "TTDB", 4) == 0 &&
                r.header.stepCount >= 0 && r.header.indexOffset >= HEADER_SIZE &&
                r.header.indexOffset + (int64_t)r.header.stepCount * 8 == fileSize;
    if (!good){
        fclose(r.file);
        r.file = NULL;
        return false;
    }



     fseek(r.file, (long)r.header.indexOffset, SEEK_SET);
    r.index.clear();
    for (int i = 0; i < r.header.stepCount; i++)
    {
        int64_t off;
        if (fread(&off, 8, 1, r.file) != 1 || off < HEADER_SIZE || off >= r.header.indexOffset)
        {
            fclose(r.file);
            r.file = NULL;
            r.index.clear();
            return false;
        }
        r.index.push_back(off);
    }
    return true;
}
void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
    r.index.clear();
    if (r.file != NULL)
        fclose(r.file);
    r.file = NULL;
}
Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
    if (step < 0 || step >= r.header.stepCount)
        return nullptr;

    fseek(r.file, (long)r.index[step], SEEK_SET);   
    Snapshot *s = new Snapshot();
    if (!readInt(r.file, s->stackDepth) || s->stackDepth < 0 || s->stackDepth > MAX_STACK_DEPTH)
    {
        delete s;
        return nullptr;
    }
    for (int i = 0; i < s->stackDepth; i++)
    {
        if (!readFrame(r.file, s->callStack[i]))
        {
            delete s;
            return nullptr;
        }
    }
    return s;
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
   
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
}
bool moveBy(DebugSession &s, int32_t delta)
{
}
bool cmdStart(DebugSession &s, const string &sourcePath)
{
    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).
}
bool cmdNext(DebugSession &s) {}
bool cmdPrv(DebugSession &s) {}
bool cmdJmpNext(DebugSession &s, int32_t n) {}
bool cmdJmpPrv(DebugSession &s, int32_t n) { }

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
}
void printHelp()
{
    // Print the commands
}

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================

bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.
}
int main(int argc, char *argv[])
{
    // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function. 

    // DebugSession session; initSession(session);
    // Print a short welcome and the help text.
    //
    // Infinite loop:
    //   if quitRequested, break
    //
    // After the loop: closeSession(session) and return 0.
    // Never exit the loop because of an error. Only quit or the end of the input leaves it.

}