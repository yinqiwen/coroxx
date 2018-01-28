#ifndef COROXX_SCHEDULER_H_
#define COROXX_SCHEDULER_H_

#include <stack>
#include <deque>
#include <vector>
#include <map>
#include <functional>
#include <unordered_map>
#include <pthread.h>
#include "coro_base.h"
#include "libcoro.h"
#include "thread_mutex_lock.h"

namespace coroxx
{

/*
 * A simple coroutine scheduler
 */
    class CoroDiaptchData;

    typedef LibCoroCoroutine CoroType;
    typedef CoroType* CoroPtr;

    class Scheduler
    {
        private:
            typedef std::unordered_map<coro_id, CoroPtr> CoroutineTable;
            CoroutineTable join_table;
            CoroutineTable exec_table;
            pthread_t tid;
            typedef std::deque<CoroDiaptchData*> CoroDiaptchDataQueue;
            typedef std::map<int64_t, CoroDiaptchDataQueue> TimeCoroDiaptchDataQueue;
            CoroDiaptchDataQueue coro_queue;
            TimeCoroDiaptchDataQueue coro_time_queue;
            ThreadMutexLock coro_queue_mutex;

            typedef std::vector<CoroPtr> CoroArray;
            CoroArray deleting_coros;
            CoroPtr main_coro;
            int64_t routine_wait_ms;
            int64_t exec_coro_num;
            CoroOptions options;
            void Run();
            static void* RunFunc(void* data);
            static void RoutineFunc(void* data);
            CoroPtr GetCoroutineById(coro_id id);
            CoroPtr CreateCoroutine();
            void CoroGC();
            int64_t CheckTimeQueue();
            void AddTimeCoroTask(CoroDiaptchData* data, int64_t wait_time);
            bool IsOverload();
            int PushCoroTask(const CoroutineFunction& func, void* data, bool create_coro, int64_t wait_time = 0);
        public:
            Scheduler(const CoroOptions& opt)
                    : main_coro(NULL),routine_wait_ms(10),exec_coro_num(0),options(opt)
            {
            }
            int Start();

            void Routine(bool wait);

            /*
             * Wakeup waiting coroutine
             */
            void Wakeup(CoroPtr coro);
            /*
             * Yield & wait
             */
            void Wait(CoroPtr coro);

            void CoroDone(CoroPtr coro);

            coro_id StartCoro(const CoroutineFunction& func, void* data);
            int StartCoroAfter(const CoroutineFunction& func, void* data, int64_t wait_time, bool create_coro = true);

            int Join(coro_id cid);
            int Join(coro_id c1, coro_id c2);
            int Join(coro_id c1, coro_id c2, coro_id c3);

            uint32_t GetCoroNum()
            {
                return exec_coro_num;
            }

            static bool IsInMainCoro();
    };

    class SchedulerManager
    {
        private:
            std::vector<Scheduler*> schedulers_;
            SchedulerManager(const CoroOptions& opt);
           
        public:
            static Scheduler& GetScheduler();
            static int CreateSingleton(const CoroOptions& opt);
            static SchedulerManager& Singleton();
            static uint32_t GetRunningCoroNum();
    };

    coro_id StartCoro(const CoroutineFunction& func, void* data = NULL);
    int StartCoroAfter(const CoroutineFunction& func, void* data, int64_t ms);
    int JoinCoro(coro_id cid);
    int JoinCoro(coro_id c1, coro_id c2);
    int JoinCoro(coro_id c1, coro_id c2, coro_id c3);
    CoroPtr CoroSelf();
    void WaitCoro(CoroPtr coro);
    void CoroSleep(int64_t ms);

}

#endif /* COROXX_SCHEDULER_H_ */
