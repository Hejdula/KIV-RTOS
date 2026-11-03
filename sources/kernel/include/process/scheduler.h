#pragma once
#include "process/thread.h"
#include <hal/intdef.h>

struct TThread_Struct;
struct TThread_List_Node;
enum class NGet_Sched_Info_Type;

struct TDeferred_Thread_Cleanup {
    bool do_cleanup;
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
    TDeferred_Thread_Cleanup* mCleanup_Contents;

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

    // blokuje soucasny proces (zatim v generickem stavu "Blocked"), preplanuje na jiny proces
    void Block_Current_Thread();

    // ziska info z planovace
    bool Get_Scheduler_Info(NGet_Sched_Info_Type type, void* target);

    void Set_Deferred_Cleanup(bool free_page_table);
};

extern CScheduler sScheduler;
