#ifndef COROXX_LIBCORO_H_
#define COROXX_LIBCORO_H_

#include "coro_base.h"
#include "coro.h"

namespace coroxx
{
    struct LibCoroCoroutine;
    struct CoroStack
    {
            coro_stack stack;
            LibCoroCoroutine* coro;
            bool shared;
            CoroStack(uint32_t stack_size, bool share)
                    : coro(NULL), shared(share)
            {
                coro_stack_alloc(&stack, stack_size);
            }
            ~CoroStack()
            {
                coro_stack_free(&stack);
            }
    };
    struct LibCoroCoroutine
    {
            coro_context ctx;
            CoroStack* stack;
            coro_id id;
            CoroutineDataContext data_ctx;
            bool ctx_inited;
            bool waiting;
            void* save_buffer;
            size_t save_buffer_size;
            char* stack_sp;
            LibCoroCoroutine(const CoroOptions& opt, bool main = false);
            ~LibCoroCoroutine();
            void ResetContext(const CoroutineFunction& func, void* data)
            {
                data_ctx.data = data;
                data_ctx.func = func;
            }
            CoroutineDataContext& GetDataContext()
            {
                return data_ctx;
            }
            void SaveStackBuffer();
            void ResumeStackBuffer();
            void MarkStackResumePoint();
            void Wait();
            void Yield(bool release_current = false);
            void Resume(bool release_current = false);
            coro_id Id() const
            {
                return id;
            }
            static LibCoroCoroutine* CurrentCoro();
            static void* NewShareStack(int count, int size);

    };
}
#endif /* COROXX_LIBCORO_H_ */
