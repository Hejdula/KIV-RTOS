#pragma once

#include <process/process_manager.h>

struct TThread_List_Node {
    CProcess_List_Node* prev;
    CProcess_List_Node* next;
    TProcess_Struct* process;
};

class CScheduler {
    private:    
        // prave bezici vlakno
        TThread_Struct* mCurrent_Thread;

        // pocatek listu vsech vlaken
        TThread_List_Node* mThread_list_head;

    private:
        void Switch_To(TThread_Struct* node);

        bool Notify_Thread(TThread_Struct* thread);

    protected:
        // round-robin planovac (se statickou prioritou)
        TThread_Struct* Schedule_RR();
        // earliest deadline first (real-time) planovac
        TThread_Struct* Schedule_EDF();

        TThread_Struct* (CProcess_Manager::*mSchedule_Fnc)() = nullptr;

    public:
        CScheduler();

        // metoda, kterou se explicitne spusti planovac - muze byt volana z IRQ handleru casovace, nebo treba i ze SW preruseni (kdyz se proces zablokuje)
        void Schedule();

        TThread_Struct* Get_Current_Thread() const;

        // blokuje soucasny proces (zatim v generickem stavu "Blocked"), preplanuje na jiny proces
        void Block_Current_Thread();

        // ziska info z planovace
        bool Get_Scheduler_Info(NGet_Sched_Info_Type type, void* target);
};

extern CScheduler sScheduler;
