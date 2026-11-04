#pragma once

#include "process.h"
#include "swi.h"
#include <hal/intdef.h>

// neplatny handle (procesu, souboru, ...)
constexpr uint32_t Invalid_Handle = static_cast<uint32_t>(-1);
constexpr uint32_t Invalid_PID = static_cast<uint32_t>(-1);

// podtypy pro Get_Sched_Info syscall
enum class NGet_Sched_Info_Type {
    Active_Process_Count = 0, // pocet procesu, ktere jsou aktivne planovane (Runnable + Running)
    Tick_Count = 1,           // pocet ticku casovace
    Process_Summary = 2,
};

// deadline syscall
enum class NDeadline_Subservice {
    Set_Relative = 0,  // nastaveni deadline
    Get_Remaining = 1, // ziska zbyvajici cas do deadline
};

struct CProcess_Summary_Info {
    uint32_t total;   // celkovy pocet tasku
    uint32_t running; // runnable, running
    uint32_t blocked; // blocked, interruptable_sleep
    uint32_t zombie;  // zombie
};

// struktura uzlu v seznamu procesu
struct CProcess_List_Node {
    CProcess_List_Node* prev;
    CProcess_List_Node* next;
    TProcess_Struct* process;
};

class CProcess_Manager {
  private:
    uint32_t mLast_PID;                     // posledni pridelene PID
    CProcess_List_Node* mProcess_List_Head; // uvodni uzel spojoveho seznamu procesu

  private:
  protected:
  public:
    CProcess_Manager();

    // vytvori proces, ktery bude vykonavat zadanou funkci, vraci jeho PID; volitelne muze jit o systemovy proces
    uint32_t Create_Process(unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system);

    // ukonci proces, nastavi ho jako zombie, ukonci vsechny jeho vlakna, zavre soubory,
    void Terminate_Process(TProcess_Struct* proc);

    // vrati uzel procesu ve spojovem seznamu procesu dle PID, nebo nullptr, pokud proces neexistuje
    CProcess_List_Node* Get_Process_Node_By_PID(uint32_t pid) const;

    // vrati strukturu procesu se zadanym PID, nebo nullptr, pokud proces neexistuje
    TProcess_Struct* Get_Process_By_PID(uint32_t pid) const;

    // notifikuje vsechna vlakna blokovaneho procesu spici nad necim (napr. spici nad mutexem, souborem, ...)
    bool Notify_Process(uint32_t pid, IFile* notifier_file);

    // namapuje otevreny soubor na handle
    uint32_t Map_File_To_Current(IFile* file);

    // odmapuje soubor z daneho handle
    bool Unmap_File_Current(uint32_t handle);

    // zavre vsechny soubory procesu
    void Close_All_Files(TProcess_Struct* task);

    // softwarova preruseni pro filesystem facility
    void Handle_Filesystem_SWI(NSWI_Filesystem_Service svc_idx, uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target);

    // zpracuje WaitAll syscall
    void Handle_WaitAll_SWI(uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target);

    // ziska celkovy pocet alokovanch stranek tasky
    uint32_t Get_Page_Count();
    // ziska celkovy pocet otevrenyh souboru
    uint32_t Get_File_Count();
};

extern CProcess_Manager sProcessMgr;
