#ifndef COROXX_CORO_BASE_H_
#define COROXX_CORO_BASE_H_

#include <stdint.h>
#include <stdlib.h>
#include <functional>
#include "coro.h"

namespace coroxx
{
    typedef void CoroutineFunc(void* data);
    typedef std::function<void(void*)> CoroutineFunction;
    typedef void CoroDataDestructor(void* obj);
    template<typename T>
    void StandardCoroDataDestructor(void* obj)
    {
        if (NULL != obj)
        {
            T* v = (T*) obj;
            delete v;
        }
    }
    typedef uint64_t coro_id;

    struct CoroutineDataContext
    {
            CoroutineFunction func;
            void* data;
            void* coro;
            CoroutineDataContext()
                    : data(NULL), coro(NULL)
            {
            }
    };

    struct CoroOptions
    {
            bool create_thread;
            uint32_t max_stack_size;
            uint32_t max_coro_num;
            uint32_t init_coro_pool_num;
            uint32_t sheduler_num;
            uint32_t share_stack_count;
            int eventfd;
            void* share_stack;
            CoroOptions()
                    : create_thread(true), max_stack_size(0), max_coro_num(10000), init_coro_pool_num(0), sheduler_num(
                            1), share_stack_count(16), eventfd(-1), share_stack(NULL)
            {
            }
    };
}

#endif /* COROXX_CORO_BASE_H_ */
