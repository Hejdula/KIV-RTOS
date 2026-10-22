#pragma once

#include <hal/intdef.h>

#include "process.h"
#include "swi.h"
#include "thread_manager.h"
#include <fs/filesystem.h>
#include <stdstring.h>

// neplatny handle (procesu, souboru, ...)
constexpr uint32_t Invalid_Handle = static_cast<uint32_t>(-1);
constexpr uint32_t Invalid_PID = static_cast<uint32_t>(-1);

// podtypy pro Get_Sched_Info syscall
enum class NGet_Sched_Info_Type {
    Active_Process_Count    = 0,                    // pocet procesu, ktere jsou aktivne planovane (Runnable + Running)
    Tick_Count              = 1,                    // pocet ticku casovace
    Process_Summary         = 2,
};

// deadline syscall
enum class NDeadline_Subservice {
    Set_Relative            = 0,                    // nastaveni deadline
    Get_Remaining           = 1,                    // ziska zbyvajici cas do deadline
};

struct CProcess_Summary_Info {
    uint32_t total;                                 // celkovy pocet tasku
    uint32_t running;                               // runnable, running
    uint32_t blocked;                               // blocked, interruptable_sleep
    uint32_t zombie;                                // zombie
};

// struktura uzlu v seznamu procesu
struct CProcess_List_Node {
    CProcess_List_Node* prev;
    CProcess_List_Node* next;
    TProcess_Struct* process;
};

class CProcess_Manager {
    private:
        // posledni pridelene PID
        uint32_t mLast_PID;

        // uvodni uzel spojoveho seznamu procesu
        CProcess_List_Node* mProcess_List_Head;

        // CProcess_List_Node* mCurrent_Task_Node;
        // TODO: k prave bezicimu 'tasku' se pristupovalo pomoci mCurrent_Task_Node,
        // budou se planovat vlakna a bylo by vhodne rozdelit na mCurrent_Thread(ve scheduleru) a mCurrent_Process_Node
        // na mCurrent_Task_Node je aktualne 50+ referenci, tak tohle bude treba projit jedno po druhem

        // uzel procesu prave beziciho vlakna
        CProcess_List_Node* mCurrent_Process_Node;

    private:

    protected:

    public:
        CProcess_Manager();

        // vytvori proces, ktery bude vykonavat zadanou funkci, vraci jeho PID; volitelne muze jit o systemovy proces
        uint32_t Create_Process(unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system);

        // vrati strukturu procesu, ktery je v tento moment naplanovan
        TProcess_Struct* Get_Current_Process() const;

        // vrati uzel procesu ve spojovem seznamu procesu dle PID, nebo nullptr, pokud proces neexistuje
        CProcess_List_Node* Get_Process_Node_By_PID(uint32_t pid) const;
        
        // vrati strukturu procesu se zadanym PID, nebo nullptr, pokud proces neexistuje
        TProcess_Struct* Get_Process_By_PID(uint32_t pid) const;

        // notifikuje blokovany proces (napr. spici nad mutexem, souborem, ...)
        bool Notify_Process(uint32_t pid, IFile* notifier_file);

        // namapuje otevreny soubor na handle
        uint32_t Map_File_To_Current(IFile* file);

        // odmapuje soubor z daneho handle
        bool Unmap_File_Current(uint32_t handle);


        // zavre vsechny soubory procesu
        void Close_All_Files(TProcess_Struct* task);

        // softwarova preruseni pro process facility
        void Handle_Process_SWI(NSWI_Process_Service svc_idx, uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target);
        // softwarova preruseni pro filesystem facility
        void Handle_Filesystem_SWI(NSWI_Filesystem_Service svc_idx, uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target);

        // zpracuje WaitAll syscall
        void Handle_WaitAll_SWI(uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result &target);

        // ziska celkovy pocet alokovanch stranek tasky
        uint32_t Get_Page_Count();
        // ziska celkovy pocet otevrenyh souboru
        uint32_t Get_File_Count();
};

extern CProcess_Manager sProcessMgr;
