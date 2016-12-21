/*
 * httpserver2-threads.cc: Multithreaded HTTP server example
 *
 * Copyright (C) 2003-2017 by Thomas Dreibholz
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Contact: dreibh@iem.uni-due.de
 */

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <neat-socketapi.h>


#include "thread.h"
#include "ansistyle.h"
#include "safeprint.h"


using namespace std;


static const char* properties = "{\
    \"transport\": [\
        {\
            \"value\": \"SCTP\",\
            \"precedence\": 1\
        },\
        {\
            \"value\": \"TCP\",\
            \"precedence\": 1\
        }\
    ]\
}";\


class ServiceThread : public Thread
{
   public:
   ServiceThread(int sd);
   ~ServiceThread();

   inline bool hasFinished() {
      return(SocketDesc < 0);
   }

   private:
   void run();

   unsigned int ID;
   int          SocketDesc;
};


ServiceThread::ServiceThread(int sd)
{
   static unsigned int IDCounter = 0;
   ID         = ++IDCounter;
   SocketDesc = sd;
   cout << "Starting thread " << ID << "..." << endl;
   start();
}

ServiceThread::~ServiceThread()
{
   cout << "Stopping thread " << ID << "..." << endl;
   if(SocketDesc >= 0) {
      nsa_close(SocketDesc);
   }
   waitForFinish();
   cout << "Thread " << ID << " has been stopped." << endl;
}

void ServiceThread::run()
{
   // ====== Get command ==================================================
   char   command[1024];
   size_t cmdpos = 0;
   while(cmdpos < sizeof(command) - 1) {
      const ssize_t r = nsa_read(SocketDesc, &command[cmdpos], 1);
      if(r <= 0) {
         nsa_close(SocketDesc);
         SocketDesc = -1;
         return;
      }
      if(command[cmdpos] == '\r') {
         command[cmdpos] = 0x00;
         break;
      }
      cmdpos++;
   }

   cout << "Command: ";
   safePrint(cout, command, cmdpos);
   cout << endl;

   // ====== Execute HTTP GET command =====================================
   ssize_t result = 0;
   if(strncasecmp(command, "GET ", 4) == 0) {
      std::string fileName = std::string((const char*)&command[4]);
      fileName = fileName.substr(0, fileName.find(' '));   // Remove <space>HTTP/1.x
      while(fileName[0] == '/') {                          // No absolute paths!
         fileName.erase(0, 1);
      }
      if(fileName == "") {   // No file name -> index.html
         fileName = "index.html";
      }

      if(fileName[0] != '.') {   // No access to top-level directories!
         cout << "Thread " << ID << ": Trying to upload file \""
              << fileName << "\"..." << endl;
         ifstream is(fileName.c_str(), ios::binary);
         if(is.good()) {
            const char* status = "HTTP/1.0 200 OK\r\n\r\n";
            result = nsa_write(SocketDesc, status, strlen(status));

            char str[8192];
            streamsize s = is.rdbuf()->sgetn(str, sizeof(str));
            while((s > 0) && (result > 0)) {
               result = nsa_write(SocketDesc, str, s);
               s = is.rdbuf()->sgetn(str, sizeof(str));
            }
         }
         else {
            cout << "Thread " << ID << ": File <" << fileName << "> not found!" << endl;
            const char* status = "HTTP/1.0 404 Not Found\r\n\r\n404 Not Found\r\n";
            result = nsa_write(SocketDesc, status, strlen(status));
         }
      }
      else {
         cout << "Thread " << ID << ": Request for . or .. not acceptable!" << endl;
         const char* status = "HTTP/1.0 406 Not Acceptable\r\n\r\n406 Not Acceptable\r\n";
         result = nsa_write(SocketDesc, status, strlen(status));
      }
   }
   else {
      cout << "Thread " << ID << ": Bad request!" << endl;
      const char* status = "HTTP/1.0 400 Bad Request\r\n\r\n400 Bad Request\r\n";
      result = nsa_write(SocketDesc, status, strlen(status));
   }


   // ====== Shutdown connection ==========================================
   nsa_shutdown(SocketDesc, SHUT_RDWR);
   nsa_close(SocketDesc);
   SocketDesc = -1;
}




class ServiceThreadList
{
   public:
   ServiceThreadList();
   ~ServiceThreadList();
   void add(ServiceThread* thread);
   void remove(ServiceThread* thread);
   void removeFinished();
   void removeAll();

   private:
   struct ThreadListEntry {
      ThreadListEntry* Next;
      ServiceThread*   Object;
   };
   ThreadListEntry* ThreadList;
};

ServiceThreadList::ServiceThreadList()
{
   ThreadList = NULL;
}

ServiceThreadList::~ServiceThreadList()
{
   removeAll();
}

void ServiceThreadList::removeFinished()
{
   ThreadListEntry* entry = ThreadList;
   while(entry != NULL) {
      ThreadListEntry* next = entry->Next;
      if(entry->Object->hasFinished()) {
         remove(entry->Object);
      }
      entry = next;
   }
}

void ServiceThreadList::removeAll()
{
   ThreadListEntry* entry = ThreadList;
   while(entry != NULL) {
      remove(entry->Object);
      entry = ThreadList;
   }
}

void ServiceThreadList::add(ServiceThread* thread)
{
   ThreadListEntry* entry = new ThreadListEntry;
   entry->Next   = ThreadList;
   entry->Object = thread;
   ThreadList    = entry;
}

void ServiceThreadList::remove(ServiceThread* thread)
{
   ThreadListEntry* entry = ThreadList;
   ThreadListEntry* prev  = NULL;
   while(entry != NULL) {
      if(entry->Object == thread) {
         if(prev == NULL) {
            ThreadList = entry->Next;
         }
         else {
            prev->Next = entry->Next;
         }
         delete entry->Object;
         entry->Object = NULL;
         delete entry;
         return;
      }
      prev  = entry;
      entry = entry->Next;
   }
}




int ServerSocket = -1;

void intHandler(int signum)
{
   if(ServerSocket >= 0) {
      fputs("*** Ctrl-C ***\n", stderr);
      nsa_close(ServerSocket);
      ServerSocket = -1;
   }
}


int main(int argc, char** argv)
{
   if(argc < 2) {
      cerr << "Usage: " << argv[0] << " [Port]" << endl;
      exit(1);
   }


   // ====== Get remote address (resolve hostname and service if necessary) ==
   struct addrinfo* ainfo = NULL;
   struct addrinfo  ainfohint;
   memset((char*)&ainfohint, 0, sizeof(ainfohint));
   // AI_PASSIVE will set address to the ANY address.
   ainfohint.ai_flags    = AI_PASSIVE;
   ainfohint.ai_family   = AF_UNSPEC;
   ainfohint.ai_socktype = SOCK_STREAM;
   ainfohint.ai_protocol = IPPROTO_TCP;
   int error = getaddrinfo(NULL, argv[1], &ainfohint, &ainfo);
   if(error != 0) {
      cerr << "ERROR: getaddrinfo() failed: " << gai_strerror(error) << endl;
      exit(1);
   }


   // ====== Create socket of appropriate type ===============================
   ServerSocket = nsa_socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol, properties);
   if(ServerSocket <= 0) {
      perror("nsa_socket() call failed");
      exit(1);
   }


   // ====== Bind to local port ==============================================
   if(nsa_bind(ServerSocket, ainfo->ai_addr, ainfo->ai_addrlen) < 0) {
      perror("nsa_bind() call failed");
      exit(1);
   }


   // ====== Turn socket into "listen" mode ==================================
   if(nsa_listen(ServerSocket, 10) < 0) {
      perror("nsa_listen() call failed");
   }


   // ====== Install SIGINT handler ==========================================
   signal(SIGINT, &intHandler);


   // ====== Print information ===============================================
   char localHost[512];
   char localService[128];
   error = getnameinfo(ainfo->ai_addr, ainfo->ai_addrlen,
                       (char*)&localHost, sizeof(localHost),
                       (char*)&localService, sizeof(localService),
                       NI_NUMERICHOST);
   if(error != 0) {
      cerr << "ERROR: getnameinfo() failed: " << gai_strerror(error) << endl;
      exit(1);
   }
   cout << "Waiting for requests at address "
        << localHost << ", service " << localService << "..." << endl;


   // ====== Handle requests =================================================
   ServiceThreadList stl;
   for(;;) {
      // ====== Accept connection ============================================
      sockaddr_storage remoteAddress;
      socklen_t        remoteAddressLength = sizeof(remoteAddress);
      const int        newSD = nsa_accept(ServerSocket, (sockaddr*)&remoteAddress, &remoteAddressLength);
      if(newSD < 0) {
         break;
      }

      // ====== Delete finished threads ======================================
      stl.removeFinished();

      // ====== Print information ============================================
      char remoteHost[512];
      char remoteService[128];
      error = getnameinfo((sockaddr*)&remoteAddress, remoteAddressLength,
                          (char*)&remoteHost, sizeof(remoteHost),
                          (char*)&remoteService, sizeof(remoteService),
                          NI_NUMERICHOST);
      if(error != 0) {
         cerr << "ERROR: getnameinfo() failed: " << gai_strerror(error) << endl;
         exit(1);
      }
      cout << "Got connection from "
           << remoteHost << ", service " << remoteService << ":" << endl;


      // ====== Start new service thread =====================================
      stl.add(new ServiceThread(newSD));
   }


   // ====== Clean up ========================================================
   stl.removeAll();
   freeaddrinfo(ainfo);
   if(ServerSocket >= 0) {
      nsa_close(ServerSocket);
   }
   nsa_cleanup();

   cout << endl << "Terminated!" << endl;
   return(0);
}
