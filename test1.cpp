#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include "scheduler.h"

using namespace coroxx;

int32_t random_between_int32(int32_t min, int32_t max) {
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
    printf("###testFunc:%d\n", self->id);
    CoroSleep(2000);
    printf("###testFunc:%d after 2000\n", self->id);
    auto f = [](void* data)
    {
        CoroPtr self = CoroSelf();
        printf("###subrun:%d\n", self->id);
        int32_t sleep_ms = 2000 + 1000 - self->id;
        int64_t start = mstime();
        CoroSleep(sleep_ms);
        printf("###subTestFunc:%d after %lldms actual %lldms\n", self->id, sleep_ms, mstime() - start);
    };
    auto cid = StartCoro(f, NULL);
    printf("###Join coro:%d\n", cid);
    JoinCoro(cid);
    printf("###Join coro done:%d\n", cid);
}

int main()
{
    CoroOptions opt;
    opt.share_stack_count = 1;
    SchedulerManager::CreateSingleton(opt);
    for (int i = 0; i < 1000; i++)
    {
        StartCoro(testFunc, NULL);
    }

    sleep(100);
    return 0;
}
