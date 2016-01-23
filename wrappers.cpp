#include "stdafx.h"
#include "wrappers.h"

#include "server.h"


ThreadPool::ThreadPool(server* host) : host(host)
{
    int g_workers_count =
#ifndef _DEBUG
        g_worker_threads_per_processor * get_proc_count();
#else
        2;
#endif
    threads.reserve(g_workers_count);
    std::cout << "Threads count: " << g_workers_count << std::endl;
    for (auto i = 0; i < g_workers_count;i++)
    {
        // TODO: check error code
        HANDLE a = CreateThread(nullptr, 0, server::WorkerThread, host, 0, nullptr);
        if (a == INVALID_HANDLE_VALUE)
        {
            closethreads();
            break;
        }
        threads.push_back(a);
    }
}

void ThreadPool::closethreads()
{
    for (size_t i = 0;i < threads.size(); i++)
    {
        //Signal for threads - if they get NULL context, they shutdown.
        //PostQueuedCompletionStatus(host->g_io_completion_port, 0, 0, nullptr);
        host->IOCP.post(nullptr, nullptr, 0);
    }
    for (size_t i = 0; i < threads.size(); i++)
    {
        WaitForSingleObject(threads[i], INFINITE);
    }
    threads.clear();
}