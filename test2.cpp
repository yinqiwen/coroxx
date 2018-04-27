#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>
#include "scheduler.h"
//#include "kvconf.h"

using namespace coroxx;

int32_t random_between_int32(int32_t min, int32_t max)
{
    srandom(time(NULL));
    int diff = max - min;
    return (int) (((double) (diff + 1) / RAND_MAX) * rand() + min);
}

static long long ustime(void)
{
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long) tv.tv_sec) * 1000000;
    ust += tv.tv_usec;
    return ust;
}

long long mstime(void)
{
    return ustime() / 1000;
}

static void testFunc(void *data)
{
    CoroPtr self = CoroSelf();
    //printf("###testFunc:%d\n", self->id);
    CoroSleep(2000);
    auto f = [](void* data)
    {
        CoroPtr self = CoroSelf();
        //printf("###subrun:%d\n", self->id);
            CoroSleep(2000 + random_between_int32(1000, 2000));
            //printf("###subTestFunc:%d\n", self->id);
        };
    auto cid = StartCoro(f, NULL);
    //printf("###Join coro:%d\n", cid);
    JoinCoro(cid);
    //printf("###Join coro done:%d\n", cid);
}
static bool route_running = true;
static void routineFunc(void *data)
{
    Scheduler* s = (Scheduler*) data;
    while (route_running)
    {
        CoroPtr self = CoroSelf();
        s->Routine(false);
        self->Yield();
    }
}

int main()
{
//    setConfFileLoc("./scanner.conf");
    struct rusage r_usage;
    getrusage(RUSAGE_SELF, &r_usage);
    // Print the maximum resident set size used (in kilobytes).
    printf("Start Memory usage: %ld kilobytes\n", r_usage.ru_maxrss);
    int64_t print_time = mstime();
    int64_t start = mstime();
    CoroOptions opt;
    opt.share_stack_count = 0;
    opt.max_stack_size = 128 * 1024;
    opt.create_thread = false;
    opt.init_coro_pool_num = 0;
    opt.eventfd = 100;
    Scheduler s(opt);
    s.Start();
    coro_id rid = s.StartCoro(routineFunc, &s);
    for (int i = 0; i < 1; i++)
    {
        s.StartCoro(testFunc, NULL);
    }

    while (1)
    {
        s.GetCoroById(rid)->Resume();
        usleep(1000);
        if (mstime() - print_time > 1000)
        {
            if (mstime() - start < 30000)
            {
                for (int i = 0; i < 1000; i++)
                {
                    s.StartCoro(testFunc, NULL);
                }
            }

            print_time = mstime();
            getrusage(RUSAGE_SELF, &r_usage);
            printf("Start Memory usage: %ld kilobytes, coro num:%d\n", r_usage.ru_maxrss, s.GetCoroNum());

        }
        if (s.GetCoroNum() == 1)
        {
            route_running = false;
            s.GetCoroById(rid)->Resume();
            break;
        }
    }
    while(s.GetCoroNum() != 0)
    {
        s.Routine(false);
        usleep(1000);
    }
    return 0;
}
