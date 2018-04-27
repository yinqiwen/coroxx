#include "libcoro.h"
#include "thread_local.h"
#include "thread_mutex.h"
#include <stack>
#include <vector>
#include <set>
#include <string.h>

namespace coroxx
{
    typedef std::stack<LibCoroCoroutine*> CoroutineStack;
    static uint64_t g_coro_id_seed = 0;

    struct CoroShareStackPool
    {
            std::vector<CoroStack*> pool;
            size_t cursor;
            CoroShareStackPool()
                    : cursor(0)
            {
            }
            size_t size() const
            {
                return pool.size();
            }

    };

    static void swapCoroFunc(void* data);
    struct SwapContext
    {
            LibCoroCoroutine* from;
            LibCoroCoroutine* to;
            coro_context swap_coro;
            coro_stack swap_stack;
            bool inited;
            SwapContext()
                    : from(NULL), to(NULL), inited(false)
            {
            }
            void Init()
            {
                if (inited)
                {
                    return;
                }
                coro_stack_alloc(&swap_stack, 0);
                coro_create(&swap_coro, swapCoroFunc, NULL, swap_stack.sptr, swap_stack.ssze);
                inited = true;
            }
    };

    struct CoroEnv
    {
            LibCoroCoroutine* current;
            LibCoroCoroutine* main;
            CoroutineStack coro_stack;
            CoroShareStackPool share_stack_pool;
            SwapContext swap_ctx;
            uint32_t stack_size;
            uint32_t share_stack_count;

            CoroEnv()
                    : current(NULL), main(NULL), stack_size(0), share_stack_count(0)
            {
            }
            CoroStack* GetCoroStack()
            {
                if (share_stack_pool.size() < share_stack_count)
                {
                    CoroStack* s = new CoroStack(stack_size, true);
                    share_stack_pool.pool.push_back(s);
                    return s;
                }
                if (share_stack_pool.pool.empty())
                {
                    return new CoroStack(stack_size, false);
                }
                size_t idx = share_stack_pool.cursor;
                if (idx >= share_stack_pool.size())
                {
                    idx = 0;
                    share_stack_pool.cursor = 0;
                }
                share_stack_pool.cursor++;
                return share_stack_pool.pool[idx];
            }
    };
    static ThreadLocal<CoroEnv> g_env;
    static ThreadMutex g_create_mutex;
    static inline CoroEnv& getCoroEnv()
    {
        return g_env.GetValue();
    }

    static void LibCoroFunc(void* data)
    {
        CoroutineDataContext* ctx = (CoroutineDataContext*) data;
        ctx->func(ctx->data);
    }

    static void swapCoroFunc(void* data)
    {
        while (1)
        {
            CoroEnv& coro_env = getCoroEnv();
            SwapContext& swap = coro_env.swap_ctx;
            swap.from->SaveStackBuffer();
            swap.to->ResumeStackBuffer();

            //printf("####%d resumex %d\n", swap.to->id,coro_env.current->id);
            coro_env.current = swap.to;
            coro_transfer(&(swap.swap_coro), &(swap.to->ctx));
        }
    }

    LibCoroCoroutine::LibCoroCoroutine(const CoroOptions& opt, bool main)
            : stack(NULL), id(0), ctx_inited(false), waiting(false), save_buffer(NULL), save_buffer_size(0), stack_sp(
            NULL)
    {
        id = g_coro_id_seed++;
        data_ctx.coro = this;

        g_create_mutex.Lock();
        if (main)
        {
            coro_create(&ctx, NULL, NULL, NULL, 0);
            ctx_inited = true;
        }
        g_create_mutex.Unlock();
    }

    void LibCoroCoroutine::SaveStackBuffer()
    {
        if (NULL != stack && stack->shared && NULL != stack_sp)
        {
            if (NULL != save_buffer)
            {
                free(save_buffer);
            }
            save_buffer = malloc(save_buffer_size);
            memcpy(save_buffer, stack_sp, save_buffer_size);
        }
    }
    void LibCoroCoroutine::ResumeStackBuffer()
    {
        if (NULL != save_buffer && NULL != stack && stack->shared)
        {
            memcpy(stack_sp, save_buffer, save_buffer_size);
            free(save_buffer);
            save_buffer = NULL;
            stack_sp = NULL;
            save_buffer_size = 0;

        }
    }
    void LibCoroCoroutine::MarkStackResumePoint()
    {
        if (NULL != stack && stack->shared)
        {
            char c;
            stack_sp = &c;
            save_buffer_size = ((char*) stack->stack.sptr + stack->stack.ssze) - stack_sp;
            //printf("###[%d, %lld]Save buffer size:%lld\n", id, stack->stack.ssze, save_buffer_size);
        }
    }

    LibCoroCoroutine::~LibCoroCoroutine()
    {
        if (NULL != stack)
        {
            if (stack->shared)
            {
                if (stack->coro == this)
                {
                    stack->coro = NULL;
                }
            }
            else
            {
                delete stack;
            }
        }
        if (NULL != save_buffer)
        {
            free(save_buffer);
        }
        coro_destroy(&ctx);
    }

    void LibCoroCoroutine::Wait()
    {
        waiting = true;
        Yield(false);
    }

    void LibCoroCoroutine::Yield(bool release_current)
    {
        CoroutineStack& coro_stack = getCoroEnv().coro_stack;
        LibCoroCoroutine* next = getCoroEnv().main;
        if (!coro_stack.empty())
        {
            next = coro_stack.top();
            coro_stack.pop();
        }
        next->Resume(release_current);

    }
    void LibCoroCoroutine::Resume(bool release_current)
    {
        CoroEnv& coro_env = getCoroEnv();
        CoroutineStack& coro_stack = getCoroEnv().coro_stack;
        LibCoroCoroutine* current = coro_env.current;
        coro_context* current_ctx = NULL;
        if (NULL == current)
        {
            if (NULL == coro_env.main)
            {
                CoroOptions opt;
                coro_env.main = new LibCoroCoroutine(opt, true);
            }
            current_ctx = &(coro_env.main->ctx);
        }
        else
        {
            current_ctx = &(current->ctx);
        }
        if (NULL != current && !current->waiting && !release_current)
        {
            coro_stack.push(current);
        }
        coro_env.current = this;
        waiting = false;
        if (NULL == stack && !ctx_inited)
        {
            stack = coro_env.GetCoroStack();
            //stack = new CoroStack(coro_env.stack_size, false);
        }

        LibCoroCoroutine* save_coro = NULL;
        if (NULL != stack && stack->coro && stack->coro != this)
        {
            save_coro = stack->coro;
            if (save_coro == current && release_current)
            {
                save_coro = NULL;
            }
        }
        if (NULL != stack)
        {
            stack->coro = this;
        }
//        if(NULL != save_coro)
//        {
//            //save_coro->MarkStackResumePoint();
//        }
        if (!ctx_inited)
        {
            g_create_mutex.Lock();
            coro_create(&ctx, LibCoroFunc, &data_ctx, stack->stack.sptr, stack->stack.ssze);
            g_create_mutex.Unlock();
            ctx_inited = true;
        }

        if (NULL != save_coro && current != save_coro)
        {
            save_coro->SaveStackBuffer();
        }
        if (NULL != current)
        {
            current->MarkStackResumePoint();
            if (current->stack == stack)
            {
                //printf("####WARN change current stack buffer for %d->%d\n", current->id, id);
                coro_env.swap_ctx.Init();
                coro_env.swap_ctx.from = current;
                coro_env.swap_ctx.to = this;
                coro_transfer(&(current->ctx), &(coro_env.swap_ctx.swap_coro));
                return;
            }
            ResumeStackBuffer();
        }
        coro_transfer(current_ctx, &ctx);

    }
    LibCoroCoroutine* LibCoroCoroutine::CurrentCoro()
    {
        return getCoroEnv().current;
    }
    void* LibCoroCoroutine::NewShareStack(int count, int size)
    {
        getCoroEnv().share_stack_count = count;
        getCoroEnv().stack_size = size;
        return NULL;
    }
}
