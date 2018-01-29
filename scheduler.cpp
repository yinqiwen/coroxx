#include "scheduler.h"
#include "thread_local.h"
#include <errno.h>

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
            CoroCallData()
             : coro(NULL), data(NULL)
            {
            }
    };

    static void CoroutineNonPoolFunc(void* cdata)
    {
        CoroCallData* data = (CoroCallData*) cdata;
        Scheduler* scheduler = g_current_scheduler.GetValue();
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
        scheduler->CoroDone(coro);

    }

    void* Scheduler::RunFunc(void* data)
    {
        Scheduler* s = (Scheduler*)data;
        s->Run();
        delete s;
        return NULL;
    }

    int Scheduler::Start()
    {
        pthread_attr_t attr;
		pthread_attr_init(&attr);
        if (0 != pthread_create(&tid, &attr, RunFunc, this))
		{
			return errno;
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
        while(!coro_time_queue.empty())
        {
            TimeCoroDiaptchDataQueue::iterator it = coro_time_queue.begin();
            CoroDiaptchDataQueue& queue = it->second;
            int64_t ts = it->first;
            int64_t now = currentMills();
            if(now < ts)
            {
                return ts - now;
            }
            //printf("###time queue size:%d\n", queue.size());
            for(size_t i = 0; i < queue.size(); i++)
            {
                 CoroDiaptchData* data = queue[i];
                 data->Invoke();
            }
            coro_time_queue.erase(it);
        }
        return 0;
    }

    static int64_t nowMs()
    {
        struct timeval tp;
        struct timespec ts;
        gettimeofday(&tp, NULL);
        return (((int64_t) tp.tv_sec) * 1000 * 1000 + tp.tv_usec) / 1000;
    }


    void Scheduler::Routine(bool wait)
    {
       
        //static int count = 0;
        CoroGC();
        //count++;
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
        }
        else
        {
            //printf("###Exec Data:%p\n", data);
            data->Invoke();
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
        CoroType::SetCoroOptions(options);
        main_coro = new CoroType(true);
        main_coro->ResetContext(Scheduler::RoutineFunc, this);
        main_coro->Resume();
    }

    void Scheduler::AddTimeCoroTask(CoroDiaptchData* data, int64_t wait_time)
    {
        int64_t ts = currentMills();
        ts += wait_time;
        coro_time_queue[ts].push_back(data);
    }

    int Scheduler::StartCoroAfter(const CoroutineFunction& func, void* data, int64_t wait_time, bool create_coro)
    {
        if(NULL == CoroSelf())
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
        if(wait_time > 0)
        {
           AddTimeCoroTask(task, wait_time);
        }
        else
        {
            coro_queue.push_back(task);
            coro_queue_mutex.Notify();
        }
        coro_queue_mutex.Unlock();
        return 0;
    }

    void Scheduler::Wakeup(CoroPtr coro)
    {
        //printf("####Wake Resume \n");
        coro->Resume();
    }

    void Scheduler::Wait(CoroPtr coro)
    {
        coro->Wait();
    }

    void Scheduler::CoroGC()
    {
        for(size_t i = 0 ;i < deleting_coros.size(); i++)
        {
            delete deleting_coros[i];
        }
        deleting_coros.clear();
    }

    void Scheduler::CoroDone(CoroPtr coro)
    {
        if (exec_table.erase(coro->id) > 0)
        {
            exec_coro_num--;
        }
        deleting_coros.push_back(coro);
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

    CoroPtr Scheduler::CreateCoroutine()
    {
        CoroPtr task = new CoroType();
        return task;
    }
    coro_id Scheduler::StartCoro(const CoroutineFunction& func, void* data)
    {
        if(NULL == CoroSelf())
        {
            return PushCoroTask(func, data, true, 0);
        }
        if (IsOverload())
        {
            return (coro_id) -1;
        }
        CoroPtr task = CreateCoroutine();
        coro_id id = task->id;
        exec_table[task->id] = task;
        exec_coro_num++;
        CoroutineDataContext& data_ctx = task->GetDataContext();
        CoroCallData* cdata = new CoroCallData;;
        data_ctx.data = cdata;
        cdata->data = data;
        cdata->coro = task;
        cdata->func = func;
        data_ctx.func = CoroutineNonPoolFunc;
        Wakeup(task);
        return id;
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
        CoroPtr coro = (CoroPtr)data;
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
