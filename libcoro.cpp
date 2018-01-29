#include "libcoro.h"
#include "thread_local.h"
#include "thread_mutex.h"
#include <stack>
#include <vector>
#include <set>
#include <string.h>

namespace coroxx
{
    typedef std::set<LibCoroCoroutine*> CoroutineSet;
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
            coro_context init_ctx;
            LibCoroCoroutine* current;
            LibCoroCoroutine* main;
            CoroutineSet coro_set;
            CoroShareStackPool share_stack_pool;
            SwapContext swap_ctx;
            uint32_t stack_size;
            uint32_t share_stack_count;
            CoroEnv()
                    : current(NULL), main(NULL), stack_size(0), share_stack_count(0)
            {
            }
    };
    static ThreadLocal<CoroEnv> g_env;
    static ThreadMutex g_create_mutex;
    static inline CoroEnv& getCoroEnv()
    {
        return g_env.GetValue();
    }

    static CoroStack* GetCoroStackFromPool()
    {
        CoroEnv& coro_env = getCoroEnv();
        CoroShareStackPool& pool = coro_env.share_stack_pool;
        if (pool.size() < coro_env.share_stack_count)
        {
            CoroStack* s = new CoroStack(coro_env.stack_size, true);
            pool.pool.push_back(s);
            return s;
        }
        if (pool.pool.empty())
        {
            return new CoroStack(coro_env.stack_size, false);
        }
        size_t idx = pool.cursor;
        if (idx >= pool.size())
        {
            idx = 0;
            pool.cursor = 0;
        }
        pool.cursor++;
        return pool.pool[idx];
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

    LibCoroCoroutine::LibCoroCoroutine(bool mainCoro)
            : stack(NULL), id(0), save_buffer(NULL), save_buffer_size(0), stack_sp(NULL), init_ctx(false), main_coro(
                    mainCoro), waiting(false)
    {
        id = g_coro_id_seed++;
        data_ctx.coro = this;
        if (main_coro)
        {
            getCoroEnv().main = this;
        }
        //printf("####Create %d %d\n", id, main_coro);
    }
    void LibCoroCoroutine::SaveStackBuffer()
    {
        if (NULL != stack && NULL != stack_sp)
        {

            if (NULL != save_buffer)
            {
                free(save_buffer);
            }
            save_buffer = malloc(save_buffer_size);
            memcpy(save_buffer, stack_sp, save_buffer_size);
            //printf("####SaveStackBuffer %d %d %p %p\n", id,save_buffer_size,stack_sp, save_buffer);
        }
    }
    void LibCoroCoroutine::ResumeStackBuffer()
    {
        if (NULL != save_buffer && NULL != stack)
        {
            //printf("####ResumeStackBuffer %d %d %p %p\n", id,save_buffer_size,stack_sp,save_buffer);
            memcpy(stack_sp, save_buffer, save_buffer_size);
            free(save_buffer);
            save_buffer = NULL;
            stack_sp = NULL;
            save_buffer_size = 0;

        }
    }
    void LibCoroCoroutine::MarkStackResumePoint()
    {
        if (NULL != stack)
        {
            char c;
            stack_sp = &c;
            save_buffer_size = ((char*) stack->stack->sptr + stack->stack->ssze) - stack_sp;
            // stack_sp = (char*)(stack->stack->sptr);
            // save_buffer_size = stack->stack->ssze;
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
            save_buffer = NULL;
        }
        coro_destroy(&ctx);
        CoroutineSet& coro_set = getCoroEnv().coro_set;
        coro_set.erase(this);
        //printf("####Free %d\n", id);
    }

    void LibCoroCoroutine::Wait()
    {
        waiting = true;
        Yield(false);
    }

    void LibCoroCoroutine::Yield(bool release_current)
    {
        CoroutineSet& coro_set = getCoroEnv().coro_set;
        LibCoroCoroutine* next = getCoroEnv().main;
        if (!coro_set.empty())
        {
            CoroutineSet::iterator begin = coro_set.begin();
            next = *begin;
            coro_set.erase(begin);
        }
        //printf("####Yield form %d->%d\n",  id, next->id);
        next->Resume(release_current);
    }
    void LibCoroCoroutine::Resume(bool release_current)
    {
        CoroEnv& coro_env = getCoroEnv();
        CoroutineSet& coro_set = coro_env.coro_set;
        LibCoroCoroutine* current = coro_env.current;
        coro_context* current_ctx = NULL;
        if (NULL == current)
        {
            current_ctx = &(coro_env.init_ctx);
            coro_create(current_ctx, NULL, NULL, NULL, 0);
        }
        else
        {
            current_ctx = &(current->ctx);
            //printf("####Resume form %d->%d %d\n", current->id, id, release_current);
        }
        if (NULL != current && !current->waiting && !release_current && current != coro_env.main)
        {
            coro_set.insert(current);
        }

        coro_env.current = this;
        waiting = false;
        if (NULL == stack && !init_ctx)
        {
            if (main_coro)
            {
                stack = new CoroStack(coro_env.stack_size, false);
            }
            else
            {
                stack = GetCoroStackFromPool();
            }
        }
        LibCoroCoroutine* save_coro = NULL;
        if (NULL != stack)
        {
            if (stack->coro && stack->coro != this)
            {
                save_coro = stack->coro;
                if (save_coro == current && release_current)
                {
                    save_coro = NULL;
                }
            }
            stack->coro = this;
        }

        if (!init_ctx)
        {
            //TC_LockT<TC_ThreadMutex> lock(g_coro_create_mutex);å
            g_create_mutex.Lock();
            coro_create(&ctx, LibCoroFunc, &data_ctx, stack->stack->sptr, stack->stack->ssze);
            g_create_mutex.Unlock();
            init_ctx = true;
        }

        if (NULL != save_coro)
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
        coro_transfer(current_ctx, &(this->ctx));
    }
    LibCoroCoroutine* LibCoroCoroutine::CurrentCoro()
    {
        return getCoroEnv().current;
    }
    void LibCoroCoroutine::SetCoroOptions(const CoroOptions& opt)
    {
        getCoroEnv().stack_size = opt.max_stack_size;
        getCoroEnv().share_stack_count = opt.share_stack_count;
    }
}
