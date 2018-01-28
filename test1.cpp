#include <stdio.h>
#include <unistd.h>
#include "scheduler.h"

using namespace coroxx;


static void testFunc(void *data)
{
    CoroPtr self = CoroSelf();
    printf("###testFunc:%d\n", self->id);
    CoroSleep(2000);
    auto f = [](void* data){
          CoroPtr self = CoroSelf();
          printf("###subrun:%d\n", self->id);
          CoroSleep(2000);
          printf("###subTestFunc:%d\n", self->id);
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
    for(int i = 0; i < 10000; i++)
{
    StartCoro(testFunc, NULL);
}

    sleep(100);
    return 0;
}