#ifndef CORE_CORO_LIBCO_H_
#define CORE_CORO_LIBCO_H_

#include "coro_base.h"
#include "co_routine.h"

namespace coroxx
{
    struct LibCoCoroutine
    {
            stCoRoutine_t* ctx;
            coro_id id;
            CoroutineDataContext data_ctx;
            LibCoCoroutine(const CoroOptions& opt);
            ~LibCoCoroutine();
            void ResetContext(const CoroutineFunction& func, void* data)
            {
                data_ctx.data = data;
                data_ctx.func = func;
            }
            CoroutineDataContext& GetDataContext()
            {
                return data_ctx;
            }
            void Wait();
            void Yield(bool release_current = false);
            void Resume(bool release_current = false);
            coro_id Id() const
            {
                return id;
            }
            static LibCoCoroutine* CurrentCoro();
            static void* NewShareStack(int count, int size);
    };
}

#endif /* CORE_CORO_LIBCO_H_ */
