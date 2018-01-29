#ifndef COROXX_LIBCORO_H_
#define COROXX_LIBCORO_H_

#include "coro_base.h"
#include "coro.h"

namespace coroxx
{
    struct LibCoroCoroutine;
    struct CoroStack
    {
            coro_stack* stack;
            LibCoroCoroutine* coro;
            bool shared;
            CoroStack(uint32_t stack_size, bool share)
                    : stack(NULL), coro(NULL),shared(share)
            {
                stack = (coro_stack*) malloc(sizeof(coro_stack));
                coro_stack_alloc(stack, stack_size);
                //memset(stack->sptr, 0, stack->ssze);
            }
            ~CoroStack()
            {
                //printf("##delete stack!\n");
                if (NULL != stack)
                {
                    coro_stack_free(stack);
                    free(stack);
                }
            }
    };
    struct LibCoroCoroutine
    {
            coro_context ctx;
            CoroStack* stack;
            coro_id id;
            CoroutineDataContext data_ctx;
            void* save_buffer;
            size_t save_buffer_size;
            char* stack_sp;
            bool init_ctx;
            bool main_coro;
            bool waiting;
            LibCoroCoroutine(bool mainCoro = false);
            ~LibCoroCoroutine();
            void SaveStackBuffer();
            void ResumeStackBuffer();
            void MarkStackResumePoint();
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
            static LibCoroCoroutine* CurrentCoro();
            static void SetCoroOptions(const CoroOptions& opt);

    };
}
#endif /* COROXX_LIBCORO_H_ */
