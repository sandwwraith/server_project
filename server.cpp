#include "stdafx.h"
#include "server.h"


DWORD server::WorkerThread(LPVOID param)
{
    server* me = static_cast<server*>(param);
    void* context = nullptr;
    OVERLAPPED* overlapped = nullptr;
    OVERLAPPED_EX* overlapped_ex;
    DWORD dwBytesTransfered = 0;

    while (true)
    {
        BOOL queued_result = GetQueuedCompletionStatus(me->g_io_completion_port, &dwBytesTransfered, (PULONG_PTR)&context, &overlapped, INFINITE);
        if (!queued_result)
        {
            auto err_code = GetLastError();
            std::cout << "Error dequeing: " << err_code << std::endl; //121 = ERROR_SEM_TIMEOUT; 64 - NET_NAME_INVALID, need to delete client
            if (err_code == ERROR_SEM_TIMEOUT || err_code == ERROR_NETNAME_DELETED)
            {
                me->drop_client(static_cast<Client*>(context));
            }
            if(err_code == ERROR_CONNECTION_ABORTED) //Deleted socket
            {
                delete overlapped;
            }
            continue;
        }
        if (context == nullptr) //Signal to shutdown
        {
            return 0;
        }
        Client* client = static_cast<Client*>(context);
        if (client->client_status == DUMMY)
        {
            if (me->lastAccepted != nullptr) {
                std::cout << "Accept successfull, id:" << me->lastAccepted->id << std::endl;

               int res = setsockopt(me->lastAccepted->get_socket(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                    (char*)&(me->listenSock), sizeof(me->listenSock));
                if (res == SOCKET_ERROR)
                {
                    std::cout << "Set sock opt failed: " << WSAGetLastError() << std::endl;
                }

                me->g_client_storage.attach_client(me->lastAccepted);
                if (!me->lastAccepted->send_greetings(me->g_client_storage.clients_count()))
                {
                    std::cout << "Error in inital send, drop client " << me->lastAccepted->id << std::endl;
                    me->drop_client(me->lastAccepted);
                }
                me->lastAccepted = nullptr;
                me->accept();
            }
            else
            {
                std::cout << "WTF\n";
            }
            continue;
        }
        if (dwBytesTransfered == 0) //Client dropped connection
        {
            std::cout << client->id << " Zero bytes transfered, disconnecting (#" << std::this_thread::get_id() << std::endl;
            me->drop_client(client);
            continue;
        }
        overlapped_ex = static_cast<OVERLAPPED_EX*>(overlapped);
        switch (client->client_status)
        {
        case STATE_NEW:
            //This client has just received greetings. Therefore, this operation can be only SEND
            //Switch client to receive to get QUEUE request
            client->client_status = STATE_INIT;
            if (!client->recieve())
            {
                std::cout << "Error occurred while executing WSARecv: " << WSAGetLastError() << std::endl;
                //Let's not work with this client
                me->drop_client(client);
                break;
            }

            break;
        case STATE_INIT:
            //In this state, client may ask to queue him (OP_RECV) or to get pair (OP_SEND)
            if (overlapped_ex->operation_code == OP_RECV)
            {
                if (client->get_recv_message_type() == MST_DISCONNECT)
                {
                    std::cout << client->id << " send disconnect" << std::endl;
                    me->drop_client(client);
                    break;
                }
                if (client->get_recv_message_type() != MST_QUEUE || client->q_msg.size() > 0)
                {
                    client->recieve(); // If you ignore it, maybe it will go away
                    break;
                }

                //Making a pair for our client
                if (me->g_client_queue.size() >= 1)
                {
                    client->q_msg = std::string(client->get_recv_buffer_data(), dwBytesTransfered);
                    std::cout << client->id << "Q_MSG:" << client->q_msg << std::endl;
                    me->g_client_queue.make_pair(client);
                    //Send them info
                    client->send(client->get_companion()->q_msg);
                    client->get_companion()->send(client->q_msg); //TODO: rework this also to own_companion
                }
                else
                {
                    //Enqueue user
                    client->q_msg = std::string(client->get_recv_buffer_data(), dwBytesTransfered);
                    std::cout << client->id << "Q_MSG:"<<client->q_msg<<std::endl;
                    me->g_client_queue.push(client);
                }
                client->recieve(); //Client may wish to disconnect
            }
            else
            {
                //Clients has received their themes and started messaging
                //Change only one client, 'cause we'll got two SEND complete statuses
                if (client->get_snd_message_type()== MST_QUEUE)
                {
                    client->get_companion()->q_msg.resize(0);
                    client->client_status = STATE_MESSAGING;
                }
            }
            break;
        case STATE_MESSAGING:
            //Client just sending and receiving

            if (overlapped_ex->operation_code == OP_RECV)
            {
                //We have received smth, let's send it to another client
                std::string msg(client->get_recv_buffer_data(), dwBytesTransfered);
                std::cout << client->id << " messaged " << msg << std::endl;

                //Change state if msg timeout or leave
                if (client->get_recv_message_type() == MST_TIMEOUT)
                    client->client_status = STATE_VOTING;
                if (client->get_recv_message_type() == MST_LEAVE)
                    client->client_status = STATE_INIT;
                if (client->get_recv_message_type() == MST_DISCONNECT)
                {
                    std::cout << client->id << " send disconnect" << std::endl;
                    me->drop_client(client);
                    break;
                }
                if (client->own_companion())
                {
                    if (client->get_companion()->client_status == STATE_MESSAGING)
                        client->get_companion()->send(msg);
                    client->unlock();
                }
                else
                {
                    //Handling UNEXPECTEDLY leave
                    client->send_leaved();
                }

                client->recieve();
            }
            else
            {
                // This client is already on receive, just got the message from companion
                if (client->get_snd_message_type() == MST_TIMEOUT)
                    client->client_status = STATE_VOTING;
                if (client->get_snd_message_type() == MST_LEAVE)
                    client->client_status = STATE_INIT;
            }
            break;
        case STATE_VOTING:
            //In this state, clients are only allowed to send or receive one message with results

            if (overlapped_ex->operation_code == OP_RECV)
            {
                if (client->get_recv_message_type() != MST_VOTING)
                {
                    client->recieve();
                    break;
                }
                std::string msg(client->get_recv_buffer_data(), dwBytesTransfered);
                std::cout << client->id << " voted " << msg << std::endl;
                if (client->own_companion())
                {
                    client->get_companion()->send(msg);
                    client->unlock();
                }
                else
                {
                    client->client_status = STATE_FINISHED;
                    client->send_bad_vote();
                    break;
                }

                client->client_status = STATE_FINISHED;
            }
            else
            {
                //If this client received, but didn't voted itself

                client->client_status = STATE_FINISHED;
            }

            break;
        case STATE_FINISHED:
            //In this state, client who send first, received pair message and resets
            //Client who send second delivers his message to first and also resets

            if (overlapped_ex->operation_code == OP_RECV)
            {
                std::string msg(client->get_recv_buffer_data(), dwBytesTransfered);
                std::cout << client->id << " voted " << msg << std::endl;
                if (client->own_companion())
                {
                    client->get_companion()->send(msg);
                    client->unlock();
                }
                else
                {
                    //.... nothing to do, actually.
                }
            }

            //Client may want to find another pair
            client->set_companion(nullptr);
            client->client_status = STATE_INIT;
            client->recieve();

            break;
        }
    }
}

bool server::accept()
{
    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    LPFN_ACCEPTEX lpfnAcceptEx = nullptr;
    DWORD dwBytes;
    //The most amazing function i've ever seen. 
    int res = WSAIoctl(listenSock, SIO_GET_EXTENSION_FUNCTION_POINTER
        , &GuidAcceptEx, sizeof(GuidAcceptEx), &lpfnAcceptEx, sizeof(lpfnAcceptEx)
        , &dwBytes, nullptr, nullptr);

    if (res == SOCKET_ERROR)
    {
        std::cout << "Can't get pointer: " << WSAGetLastError() << std::endl;
        return false;
    }

    SOCKET acc_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (acc_socket == INVALID_SOCKET)
    {
        std::cout << "Can't create listen socket: " << WSAGetLastError() << std::endl;
        return false;
    }

    BOOL b = lpfnAcceptEx(listenSock, acc_socket
        , accept_buf, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16
        , &dwBytes, overlapped_ac);

    if (!b && WSAGetLastError() != WSA_IO_PENDING) {
        std::cout << "AcceptEx failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    Client* cl = new Client(acc_socket);
    cl->id = ids++;
    this->lastAccepted = cl;
    HANDLE port = CreateIoCompletionPort((HANDLE)acc_socket, this->g_io_completion_port, (ULONG_PTR)cl, 0);
    if (port == nullptr)
    {
        std::cout << "Can't bind new client to comp port: " << GetLastError() << std::endl;
        this->lastAccepted = nullptr;
        delete cl;
        return false;
    }
    return true;
}

SOCKET server::create_listen_socket(server_launch_params params)
{
    //Creating overlapped socket
    SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (sock == INVALID_SOCKET)
    {
        std::cout << "Error opening socket"<< WSAGetLastError() << std::endl;
        return INVALID_SOCKET;
    }

    int res = bind(sock, reinterpret_cast<sockaddr*>(&params.serv_address), sizeof(params.serv_address));
    if (res == SOCKET_ERROR)
    {
        std::cout << "Bind error" << WSAGetLastError() << std::endl;
        closesocket(sock);
        return INVALID_SOCKET;
    }
    if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cout << "Listen failed with error:" << WSAGetLastError() << std::endl;
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

unsigned server::clients_count() const
{
    return g_client_storage.clients_count();
}

void server::drop_client(Client* cl)
{
    if (cl == nullptr) return;
    if (cl->q_msg.size() > 0) g_client_queue.remove(cl);
    if (cl->has_companion()) cl->get_companion()->delete_companion();
    g_client_storage.detach_client(cl);
    return;
}

int server::get_proc_count()
{
    if (g_processors_count == -1)
    {
        //Getting system info
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_processors_count = si.dwNumberOfProcessors;
    }
    return g_processors_count;
}

bool server::init()
{
    //Initializing WSA
    WSADATA wsaData = { 0 };
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        printf("WSAStartup failed: %d\n", iResult);
        return false;
    }

    //Initializing IOCP
    g_io_completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (g_io_completion_port == nullptr)
    {
        printf("IOCompletionPort init failed: %d\n", WSAGetLastError());
        WSACleanup();
        return false;
    }

    //Creating threads
#ifndef _DEBUG
    g_workers_count = g_worker_threads_per_processor * get_proc_count();
#else
    g_workers_count = 2;
#endif

    std::cout << "Threads count: " << g_workers_count << std::endl;
    g_worker_threads = new HANDLE[g_workers_count];

    for (auto i = 0; i < g_workers_count;i++)
    {
        g_worker_threads[i] = CreateThread(nullptr, 0, WorkerThread, this, 0, nullptr);
    }
    return true;
}

void server::shutdown()
{
    std::cout << "Server is shutting down...\n";
    for (int i = 0;i < g_workers_count; i++)
    {
        //Signal for threads - if they get NULL context, they shutdown.
        PostQueuedCompletionStatus(g_io_completion_port, 0, 0, nullptr);

    }
    //Waiting threads to shutdown
    WaitForMultipleObjects(g_workers_count, g_worker_threads, true, INFINITE);
    g_client_storage.clear_all();
    CloseHandle(g_io_completion_port);
    closesocket(listenSock);
    delete[] g_worker_threads;
    WSACleanup();
}

server::server(server_launch_params params)
{
    overlapped_ac = new OVERLAPPED{};
    accept_buf = static_cast<char*>(malloc(sizeof(char)*(2 * sizeof(sockaddr_in) + 32)));
    if (!init()) throw std::runtime_error("Error in initializing");
    listenSock = create_listen_socket(params);
    if (listenSock == INVALID_SOCKET)
    {
        std::cout << "Failed to create listen socket\n";
        shutdown();
        throw std::runtime_error("Cannot create listen socket");
    }

    acceptContext = new Client(INVALID_SOCKET);
    acceptContext->client_status = DUMMY;
    CreateIoCompletionPort((HANDLE)listenSock, g_io_completion_port, (ULONG_PTR)acceptContext, 0);
    if (!this->accept())
    {
        delete acceptContext;
        shutdown();
        throw std::exception("Cannot accept");
    }

    std::cout << "All OK, waiting for the connections" << std::endl;
}

server::~server()
{
    shutdown();
    delete acceptContext;
    delete lastAccepted;
    delete overlapped_ac;
    free(accept_buf);
}
