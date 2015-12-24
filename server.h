#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <mswsock.h>
#include <string>
#include <iostream>
#include <conio.h>

#include "client_queue.h"
#include "client.h"
#include "client_storage.h"

#pragma comment(lib,"Ws2_32.lib")

class server
{
    //Global parameter for server address/port
    int g_server_port = 2539;

    //If true, recieves on INADDR_ANY. localhost otherwise.
    bool global_addr = false;

    //Global array of worker threads
    HANDLE* g_worker_threads = nullptr;

    //Number of threads
    int g_workers_count = 0;

    //Number of processors in system
    int g_processors_count = -1;

    //Number of threads per processor
    int g_worker_threads_per_processor = 2;
public:
    int get_worker_threads_per_processor() const;
    bool set_worker_threads_per_processor(int g_worker_threads_per_processor1);

    //Returns true if port was setted (false if server already started)
    bool set_port(int new_port);
    int get_port() const;

    bool is_addr_global() const;
    bool set_global_addr(bool set_to_global);

private:
    //Main IOCP port
    HANDLE g_io_completion_port;

    //Global storage for all clients
    client_queue g_client_queue;
    client_storage g_client_storage;

    static DWORD WINAPI WorkerThread(LPVOID); //Worker function for threads
    bool g_started = false;

    SOCKET create_listen_socket();
    SOCKET listenSock;

    OVERLAPPED* overlapped_ac;
    char* accept_buf;
    Client* lastAccepted = nullptr;

    bool accept();
    unsigned long ids = 0;

    void drop_client(Client* cl);
public:

    int get_proc_count();
    unsigned int clients_count() const;
    server();
    //if init_now is true, inits server immedeately
    explicit server(bool init_now);
    bool init();

    int start();

    void shutdown();
    ~server();
};

