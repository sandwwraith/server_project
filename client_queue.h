#pragma once

#include <queue>
#include <random>
#include <chrono>
#include "client.h"
class client_queue
{
    std::default_random_engine generator; //TODO: Make real random
    std::uniform_int_distribution<int> distribution = std::uniform_int_distribution<int>(1, 50); 

    CRITICAL_SECTION sec;
    std::queue<Client*> q;
    void lock();
    void unlock();
public:
    size_t size() const;
    void push(Client* cl);
    Client* pop();

    //Makes pair and sets the theme for conversation
    void make_pair(Client * cl);
    client_queue();
    ~client_queue();
};

