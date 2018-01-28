 /*
 *Copyright (c) 2013-2013, yinqiwen <yinqiwen@gmail.com>
 *All rights reserved.
 * 
 *Redistribution and use in source and binary forms, with or without
 *modification, are permitted provided that the following conditions are met:
 * 
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Redis nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 * 
 *THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS 
 *BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
 *THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef COROXX_THREADMUTEXLOCK_HPP_
#define COROXX_THREADMUTEXLOCK_HPP_
#include "thread_mutex.h"
#include "thread_condition.h"
#include <time.h>
#include <stdint.h>
#include <sys/time.h>

namespace coroxx
{

	class ThreadMutexLock: public ThreadMutex, public ThreadCondition
	{
		private:
		     void setWaitTime(struct timespec* ts, int64_t mills)
			 {
				 int64_t nanos = mills*  1000 * 1000;
                 if (nanos < 1000000000LL)
                 {
                     ts->tv_sec = 0;
                     ts->tv_nsec = (long) nanos;
                 }
                 else
                 {
                    ts->tv_sec = nanos / 1000000000;
                    ts->tv_nsec = nanos % 1000000000;
                 }
			 }
		public:
			ThreadMutexLock()
			{
			}
			ThreadMutexLock(ThreadMutex& mutex, ThreadCondition& cond) :
					ThreadMutex(mutex), ThreadCondition(cond)
			{
			}
			//ThreadMutexLock(ThreadMutexCondition& mutexCond);
			/**
			 * wait for specified timeout value, if timeout < 0, then wait for ever
			 */
			bool Wait(uint64_t timeout = 0)
			{
				if (timeout <= 0)
				{
					return 0 == pthread_cond_wait(&m_cond, &m_mutex);
				}
				else
				{
					struct timeval tp;
                    struct timespec ts;
                    gettimeofday(&tp, NULL);
				    setWaitTime(&ts, timeout);
					return 0 == pthread_cond_timedwait(&m_cond, &m_mutex, &ts);
				}
			}
			virtual bool Notify()
			{
				return 0 == pthread_cond_signal(&m_cond);
			}
			virtual bool NotifyAll()
			{
				return 0 == pthread_cond_broadcast(&m_cond);
			}
			bool Lock()
			{
				return 0 == pthread_mutex_lock(&m_mutex);
			}
			bool Unlock()
			{
				return 0 == pthread_mutex_unlock(&m_mutex);
			}
			bool TryLock()
			{
				return 0 == pthread_mutex_trylock(&m_mutex);
			}
			virtual ~ThreadMutexLock()
			{
			}
	};
}

#endif /* COROXX_THREADMUTEXLOCK_HPP_ */
