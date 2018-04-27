#include "scheduler.h"
#include "thread_local.h"
#include <errno.h>
#include <unistd.h>

namespace coroxx
{
    static ThreadLocal<Scheduler*> g_current_scheduler;

    static int64_t currentMills()
    {
        struct timeval timeValue;
        gettimeofday(&timeValue, NULL);
        int64_t ts = ((int64_t) timeValue.tv_sec) * 1000;
        ts += ((timeValue.tv_usec) / 1000);
        return ts;
    }
    struct CoroDiaptchData
    {
            CoroutineFunction call;
            void* data;
            bool create_coro;
            CoroDiaptchData()
                    : data(NULL), create_coro(false)
            {
            }
            ~CoroDiaptchData()
            {
            }
            static void InvokeFunc(void* data)
            {
                CoroDiaptchData* v = (CoroDiaptchData*) data;
                if (NULL != v && v->call)
                {
                    v->call(v->data);
                    delete v;
                }
            }
            void Invoke()
            {
                if (create_coro)
                {
                    StartCoro(CoroDiaptchData::InvokeFunc, this);
                }
                else
                {
                    CoroDiaptchData::InvokeFunc(this);
                }
            }

    };

    struct CoroCallData
    {
            CoroPtr coro;
            void* data;
            CoroutineFunction func;
            Scheduler* scheduler;
            CoroCallData()
                    : coro(NULL), data(NULL), scheduler(NULL)
            {
            }
    };

    static void CoroutineNonPoolFunc(void* cdata)
    {
        CoroCallData* data = (CoroCallData*) cdata;
        Scheduler* scheduler = data->scheduler;
        CoroPtr coro = data->coro;
        if (data->func)
        {
            data->func(data->data);
        }
        else
        {
            abort();
        }
        delete data;
        scheduler->CoroDone(coro, false);
    }

    static void CoroutinePoolFunc(void* cdata)
    {
        CoroCallData* data = (CoroCallData*) cdata;
        Scheduler* scheduler = data->scheduler;
        CoroPtr coro = data->coro;
        while (1)
        {
            data->func(data->data);
            scheduler->CoroDone(coro, true);
        }
    }

    void* Scheduler::RunFunc(void* data)
    {
        Scheduler* s = (Scheduler*) data;
        s->Run();
        delete s;
        return NULL;
    }

    int Scheduler::Start()
    {
        options.share_stack = CoroType::NewShareStack(options.share_stack_count, options.max_stack_size);
        if (options.create_thread)
        {
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            if (0 != pthread_create(&tid, &attr, RunFunc, this))
            {
                return errno;
            }
        }
        else
        {
            g_current_scheduler.SetValue(this);
            InitCoroPool();
        }
        return 0;
    }

    CoroPtr Scheduler::GetCoroutineById(coro_id id)
    {
        CoroutineTable::iterator found = exec_table.find(id);
        if (found == exec_table.end())
        {
            return NULL;
        }
        return found->second;
    }

    int64_t Scheduler::CheckTimeQueue()
    {
        while (!coro_time_queue.empty())
        {
            TimeCoroDiaptchDataQueue::iterator it = coro_time_queue.begin();
            CoroDiaptchDataQueue& queue = it->second;
            int64_t ts = it->first;
            int64_t now = currentMills();
            if (now < ts)
            {
                return ts - now;
            }
            //printf("###time queue size:%d\n", queue.size());
            for (size_t i = 0; i < queue.size(); i++)
            {
                CoroDiaptchData* data = queue[i];
                data->Invoke();
            }
            coro_time_queue.erase(it);
        }
        return 0;
    }

    uint32_t Scheduler::InvokeQueueSize()
    {
        uint32_t n = 0;
        coro_queue_mutex.Lock();
        n = coro_queue.size();
        coro_queue_mutex.Unlock();
        return n;
    }

    void Scheduler::Routine(bool wait)
    {
        CoroGC();
        while (1)
        {
            CoroDiaptchData* data = NULL;
            {
                coro_queue_mutex.Lock();
                if ((coro_queue.empty()) && wait)
                {
                    if (routine_wait_ms <= 0 || routine_wait_ms > 100)
                    {
                        routine_wait_ms = 10;
                    }
                    //printf("###before wait %d %lld\n", routine_wait_ms, nowMs());
                    coro_queue_mutex.Wait(routine_wait_ms);
                    //printf("###after wait %d %lld %d\n", routine_wait_ms, nowMs(), v);
                }
                if (!coro_queue.empty())
                {
                    data = coro_queue.front();
                    coro_queue.pop_front();
                }
                coro_queue_mutex.Unlock();
            }

            if (NULL == data)
            {
                routine_wait_ms = CheckTimeQueue();
                return;
            }
            else
            {
                data->Invoke();
            }
        }
    }

    void Scheduler::RoutineFunc(void* data)
    {
        Scheduler* s = (Scheduler*) data;
        while (1)
        {
            s->Routine(true);
        }
    }

    void Scheduler::Run()
    {
        g_current_scheduler.SetValue(this);
        InitCoroPool();
        sched_coro = new CoroType(options);
        sched_coro->ResetContext(Scheduler::RoutineFunc, this);
        sched_coro->Resume();
    }

    void Scheduler::AddTimeCoroTask(CoroDiaptchData* data, int64_t wait_time)
    {
        int64_t ts = currentMills();
        ts += wait_time;
        coro_time_queue[ts].push_back(data);
    }

    int Scheduler::StartCoroAfter(const CoroutineFunction& func, void* data, int64_t wait_time, bool create_coro)
    {
        if (NULL == g_current_scheduler.GetValue())
        {
            return PushCoroTask(func, data, false, wait_time);
        }
        CoroDiaptchData* task = new CoroDiaptchData;
        task->call = func;
        task->data = data;
        task->create_coro = create_coro;
        AddTimeCoroTask(task, wait_time);
        return 0;
    }

    void Scheduler::InitCoroPool()
    {
        for (size_t i = 0; i < options.init_coro_pool_num; i++)
        {
            CoroutineFunction empty;
            coro_pool.push_back(CreateCoroutine(empty, NULL, true));
        }
    }

    int Scheduler::PushCoroTask(const CoroutineFunction& func, void* data, bool create_coro, int64_t wait_time)
    {
        if (create_coro && IsOverload())
        {
            return -1;
        }
        CoroDiaptchData* task = new CoroDiaptchData;
        task->call = func;
        task->data = data;
        task->create_coro = create_coro;

        coro_queue_mutex.Lock();
        if (wait_time > 0)
        {
            AddTimeCoroTask(task, wait_time);
        }
        else
        {
            coro_queue.push_back(task);
            if (options.eventfd > 0)
            {
                int64_t ev = 1;
                ::write(options.eventfd, &ev, sizeof(ev));
            }
            else
            {
                coro_queue_mutex.Notify();
            }
        }
        coro_queue_mutex.Unlock();
        return 0;
    }

    void Scheduler::Wakeup(CoroPtr coro)
    {
        coro->Resume();
    }

    void Scheduler::Wait(CoroPtr coro)
    {
        coro->Wait();
    }

    void Scheduler::CoroGC()
    {
        if(!deleting_coros.empty())
        {
            for (size_t i = 0; i < deleting_coros.size(); i++)
            {
                delete deleting_coros[i];
            }
            deleting_coros.clear();
        }
    }

    void Scheduler::CoroDone(CoroPtr coro, bool pool)
    {
        if (exec_table.erase(coro->id) > 0)
        {
            exec_coro_num--;
        }
        if (pool)
        {
            coro_pool.push_back(coro);
        }
        else
        {
            deleting_coros.push_back(coro);
        }

        /*
         * Wake join coroutine first
         */
        CoroutineTable::iterator jit = join_table.find(coro->id);
        if (jit != join_table.end())
        {
            CoroPtr waiting = jit->second;
            join_table.erase(jit);
            //printf("####Join Resume \n");
            waiting->Resume(true);
        }
        else
        {
            coro->Yield(true);
        }
    }

    bool Scheduler::IsOverload()
    {
        return false;
    }

    CoroPtr Scheduler::GetCoroutine(const CoroutineFunction& func, void* data)
    {
        if (coro_pool.empty())
        {
            return CreateCoroutine(func, data, false);
        }
        else
        {
            CoroPtr coro = coro_pool.front();
            coro_pool.pop_front();
            CoroutineDataContext& data_ctx = coro->GetDataContext();
            CoroCallData* cdata = (CoroCallData*) data_ctx.data;
            cdata->data = data;
            cdata->func = func;
            return coro;
        }
    }

    CoroPtr Scheduler::CreateCoroutine(const CoroutineFunction& func, void* data, bool pool)
    {
        CoroPtr task = new CoroType(options);
        CoroutineDataContext& data_ctx = task->GetDataContext();
        CoroCallData* cdata = new CoroCallData;
        data_ctx.data = cdata;
        cdata->data = data;
        cdata->coro = task;
        cdata->func = func;
        cdata->scheduler = this;
        if (pool)
        {
            data_ctx.func = CoroutinePoolFunc;
        }
        else
        {
            data_ctx.func = CoroutineNonPoolFunc;
        }

        return task;
    }
    coro_id Scheduler::StartCoro(const CoroutineFunction& func, void* data, bool create_coro)
    {
        //start from another thread
        if (NULL == g_current_scheduler.GetValue())
        {
            return PushCoroTask(func, data, create_coro, 0);
        }
        if (IsOverload())
        {
            return (coro_id) -1;
        }
        CoroGC();
        CoroPtr task = GetCoroutine(func, data);
        coro_id id = task->id;
        exec_table[id] = task;
        exec_coro_num++;
        Wakeup(task);
        return id;
    }

    CoroPtr Scheduler::GetCoroById(coro_id id)
    {
        CoroutineTable::iterator found = exec_table.find(id);
        if (found != exec_table.end())
        {
            return found->second;
        }
        return NULL;
    }

    int Scheduler::Join(coro_id cid)
    {
        CoroutineTable::iterator it = exec_table.find(cid);
        if (it == exec_table.end())
        {
            return 0;
        }
        it = join_table.find(cid);
        if (it != join_table.end())
        {
            return -2;
        }
        CoroPtr current = CoroType::CurrentCoro();
        join_table[cid] = current;
        Wait(current);
        return 0;
    }

    int Scheduler::Join(coro_id c1, coro_id c2)
    {
        Join(c1);
        Join(c2);
        return 0;
    }
    int Scheduler::Join(coro_id c1, coro_id c2, coro_id c3)
    {
        Join(c1);
        Join(c2);
        Join(c3);
        return 0;
    }

    bool Scheduler::IsInMainCoro()
    {
        CoroPtr current = CoroSelf();
        return NULL == current;
    }

    static SchedulerManager* g_scheduler_manager = NULL;

    SchedulerManager& SchedulerManager::Singleton()
    {
        if (NULL == g_scheduler_manager)
        {
            CoroOptions opt;
            CreateSingleton(opt);
        }
        return *g_scheduler_manager;
    }

    int SchedulerManager::CreateSingleton(const CoroOptions& opt)
    {
        if (NULL != g_scheduler_manager)
        {
            return -1;
        }
        g_scheduler_manager = new SchedulerManager(opt);
        return 0;
    }

    uint32_t SchedulerManager::GetRunningCoroNum()
    {
        if (NULL != g_scheduler_manager)
        {
            return 0;
        }
        uint32_t count = 0;
        for (size_t i = 0; i < g_scheduler_manager->schedulers_.size(); i++)
        {
            count += g_scheduler_manager->schedulers_[i]->GetCoroNum();
        }
        return count;
    }
    SchedulerManager::SchedulerManager(const CoroOptions& opt)
    {
        for (uint32_t i = 0; i < opt.sheduler_num; i++)
        {
            Scheduler* s = new Scheduler(opt);
            schedulers_.push_back(s);
            schedulers_[i]->Start();
        }
    }

    Scheduler& SchedulerManager::GetScheduler()
    {
        Scheduler* s = g_current_scheduler.GetValue();
        if (NULL != s)
        {
            return *s;
        }
        uint32_t min_coro_num = 0;
        uint32_t min_scheduler_idx = 0;
        SchedulerManager& singleton = SchedulerManager::Singleton();
        if (singleton.schedulers_.size() > 1)
        {
            for (size_t i = 0; i < singleton.schedulers_.size(); i++)
            {
                uint32_t numk = singleton.schedulers_[i]->GetCoroNum();
                if (0 == numk)
                {
                    min_scheduler_idx = i;
                    break;
                }
                if (numk < min_coro_num)
                {
                    min_coro_num = numk;
                    min_scheduler_idx = i;
                }
            }
        }
        return *(singleton.schedulers_[min_scheduler_idx]);
    }

    void WaitCoro(CoroPtr coro)
    {
        SchedulerManager::GetScheduler().Wait(coro);
    }

    coro_id StartCoro(const CoroutineFunction& func, void* data)
    {
        return SchedulerManager::GetScheduler().StartCoro(func, data);
    }
    int StartCoroAfter(const CoroutineFunction& func, void* data, int64_t ms)
    {
        return SchedulerManager::GetScheduler().StartCoroAfter(func, data, ms);
    }
    int JoinCoro(coro_id cid)
    {
        return SchedulerManager::GetScheduler().Join(cid);
    }
    int JoinCoro(coro_id c1, coro_id c2)
    {
        return SchedulerManager::GetScheduler().Join(c1, c2);
    }
    int JoinCoro(coro_id c1, coro_id c2, coro_id c3)
    {
        return SchedulerManager::GetScheduler().Join(c1, c2, c3);
    }
    CoroPtr CoroSelf()
    {
        return CoroType::CurrentCoro();
    }

    static void CoroSleepCB(void* data)
    {
        CoroPtr coro = (CoroPtr) data;
        //printf("####Sleep Resume \n");
        coro->Resume();
    }
    void CoroSleep(int64_t ms)
    {
        CoroPtr c = CoroSelf();
        SchedulerManager::GetScheduler().StartCoroAfter(CoroSleepCB, c, ms, false);
        SchedulerManager::GetScheduler().Wait(c);
    }
}
