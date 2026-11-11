#pragma once
#include "process/process_manager.h"
#include "process/thread.h"
#include <hal/intdef.h>

struct TThread_Struct;
struct TThread_List_Node;
enum class NGet_Sched_Info_Type;

struct TThreads_Summary_Info {
    uint32_t total;   // celkovy pocet vlaken v systemu
    uint32_t running; // runnable, running
    uint32_t blocked; // blocked, interruptable_sleep, blocked_on_join
    uint32_t zombie;  // zombie
};

struct TDeferred_Thread_Cleanup {
    bool do_cleanup;
    bool unlink_thread_node;
    TThread_List_Node* thread_node;
    uint32_t stack_page_virt;
    uint32_t stack_page_phys;
    bool do_free_pt;                
    uint32_t* page_table;
};

class CScheduler {
  private:
    // prave bezici vlakno
    TThread_Struct* mCurrent_Thread;
    TThread_List_Node* mCurrent_Thread_Node;
    TDeferred_Thread_Cleanup mCleanup_Contents;

  private:
    void Switch_To(TThread_List_Node* node);

  protected:
    // round-robin planovac (se statickou prioritou)
    TThread_List_Node* Schedule_RR();
    // earliest deadline first (real-time) planovac
    TThread_List_Node* Schedule_EDF();

    TThread_List_Node* (CScheduler::*mSchedule_Fnc)() = nullptr;

  public:
    CScheduler();

    // metoda, kterou se explicitne spusti planovac - muze byt volana z IRQ handleru casovace, nebo treba i ze SW preruseni (kdyz se proces zablokuje)
    void Schedule();

    TThread_Struct* Get_Current_Thread() const;
    TThread_List_Node* Get_Current_Thread_Node() const;
    TProcess_Struct* Get_Current_Process() const;
    uint32_t Get_Current_Pid() const;

    // blokuje soucasny proces (zatim v generickem stavu "Blocked"), preplanuje na jiny proces
    void Block_Current_Thread();

    // ziska info z planovace
    bool Get_Scheduler_Info(NGet_Sched_Info_Type type, void* target);

    // Oznacime stranku stacku aktualniho vlakna na uvolneni dalsim naplanovanym vlankem
    // NOTE: due to round-robin scheduler using the current thread node in calculation, 
    // we shall not unlink the current thread from thread list before invoking schedule() so that it can traverse the list
    void Sign_Up_For_Deferred_Cleanup(bool free_page_table, bool unlink_thread_node);

    // Free previous thread stack pointer page and unmap it from page table, optionally free whole page table
    void Deferred_Cleanup();
};

extern CScheduler sScheduler;
