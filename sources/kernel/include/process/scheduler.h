#pragma once

#include <process/process_manager.h>


class CScheduler {
    private:
        // prave bezici vlakno
        TThread_Struct* mCurrent_Thread;

    private:
        void Switch_To(TThread_Struct* node);
    protected:
        // round-robin planovac (se statickou prioritou)
        TThread_Struct* Schedule_RR();
        // earliest deadline first (real-time) planovac
        TThread_Struct* Schedule_EDF();

        TThread_Struct* (CScheduler::*mSchedule_Fnc)() = nullptr;

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
