// OS::Mutex out-of-line members, normally in Libs/Source/Simulator/OS/OS.cpp.
// That file also defines OS::Semaphore, which uses sem_timedwait() and therefore
// does not build on macOS; the logger only needs the mutex.
#include "SDK/Simulator/OS/OS.hpp"

namespace OS {

Mutex::Mutex() {}
void Mutex::lock() { mMutex.lock(); }
void Mutex::unLock() { mMutex.unlock(); }
bool Mutex::tryLock() { return mMutex.try_lock(); }
std::mutex& Mutex::native() { return mMutex; }

} // namespace OS
