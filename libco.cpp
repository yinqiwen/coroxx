#include "libco.h"
#include "thread_local.h"
#include "thread_mutex.h"
#include <stack>
#include <unordered_map>
namespace coroxx
{
    static uint64_t g_coro_id_seed = 0;

    static void* LibCoroFunc(void* data)
    {
        CoroutineDataContext* ctx = (CoroutineDataContext*) data;
        ctx->func(ctx->data);
        return NULL;
    }

    LibCoCoroutine::LibCoCoroutine(const CoroOptions& opt)
            : ctx(NULL), id(0)
    {
        id = g_coro_id_seed++;
        data_ctx.coro = this;
        stCoRoutineAttr_t attr;
        stShareStack_t* share_stack = (stShareStack_t*) opt.share_stack;
        attr.share_stack = share_stack;
        if (NULL == share_stack)
        {
            attr.stack_size = opt.max_stack_size;
        }
        else
        {
            attr.stack_size = 0;
        }
        co_create(&ctx, &attr, LibCoroFunc, &data_ctx);
        co_attach(ctx, this);
    }

    LibCoCoroutine::~LibCoCoroutine()
    {
        co_release(ctx);
        //pthread_key_delete(co_key);
    }
    void LibCoCoroutine::Wait()
    {
        Yield();
    }
    void LibCoCoroutine::Yield(bool release_current)
    {
        co_yield(ctx);
    }
    void LibCoCoroutine::Resume(bool release_current)
    {
        if (data_ctx.func)
        {
            co_resume(ctx);
        }
    }
    LibCoCoroutine* LibCoCoroutine::CurrentCoro()
    {
        stCoRoutine_t* self = co_self();
        if (NULL != self)
        {
            return (LibCoCoroutine*) co_getattach(self);
        }
        return NULL;
    }
    void* LibCoCoroutine::NewShareStack(int count, int size)
    {
        if (count <= 0)
        {
            return NULL;
        }
        if (size <= 0)
        {
            size = 256 * 1024;
        }
        return co_alloc_sharestack(count, size);
    }
}

