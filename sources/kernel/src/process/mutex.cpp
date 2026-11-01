#include <process/mutex.h>
#include <process/spinlock.h>
#include <process/process_manager.h>
#include <process/scheduler.h>
#include <process/resource_manager.h>
#include <process/thread.h>
#include <process/thread_manager.h>
#include <stdstring.h>

CMutex::CMutex()
    : IFile(NFile_Type_Major::Mutex) {
    spinlock_init(&mLock_State);
}

bool CMutex::Lock() {
    auto* cur = sScheduler.Get_Current_Thread();
    const PIDTID cpidtid = CThread_Manager::GetPIDTID(cur);

    if (mHolder_PIDTID == cpidtid) {
        return false;
    }

    while (spinlock_try_lock(&mLock_State) == Lock_Locked) { // try_lock vraci puvodni hodnotu zamku - pokud vrati "zamceno", zamknout se jiste nepovedlo
        // vlozime volajici vlakno do fronty cekajicich
        Wait_Enqueue_Current();

        // NOTE: tady by mohlo byt vhodne zavest podstav pro blokovany stav nad mutexem (a volat pak s parametrem)
        sScheduler.Block_Current_Thread();
    }

    mHolder_PIDTID = cpidtid;

    return true;
}

bool CMutex::Try_Wait_All_Reserve(uint32_t count) {
    auto* cur = sScheduler.Get_Current_Thread();
    const PIDTID cpidtid = CThread_Manager::GetPIDTID(cur);

    // tady vracime true, protoze proces uz mutex drzi
    if (mHolder_PIDTID == cpidtid) {
        return true;
    }

    if (spinlock_try_lock(&mLock_State) == Lock_Locked) {
        return false;
    }

    mHolder_PIDTID = cpidtid;
    return true;
}

bool CMutex::Wait_All_Acquire(uint32_t count)  {
    // udela to same jako Try_Wait_All_Reserve, pokusi se zamknout mutex
    return Try_Wait_All_Reserve(count);
}

bool CMutex::Try_Lock() {
    auto* cur = sScheduler.Get_Current_Thread();
    const PIDTID cpidtid = CThread_Manager::GetPIDTID(cur);

    if (mHolder_PIDTID == cpidtid) {
        return false;
    }

    return spinlock_try_lock(&mLock_State);
}

bool CMutex::Unlock() {
    auto* cur = sScheduler.Get_Current_Thread();
    const PIDTID cpidtid = CThread_Manager::GetPIDTID(cur);

    if (mHolder_PIDTID != cpidtid || mLock_State != Lock_Locked) {
        return false;
    }

    mHolder_PIDTID = {Invalid_PID, Invalid_TID}; //miluju in-place konstrukci structu
    spinlock_unlock(&mLock_State);

    return IFile::Notify(1);
}

bool CMutex::Close() {
    sProcess_Resource_Manager.Free_Mutex(this);

    return true;
}
