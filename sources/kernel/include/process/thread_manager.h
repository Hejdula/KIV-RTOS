#pragma once
#include <hal/intdef.h>

#include "process.h"
#include "swi.h"
#include "thread.h"

struct TThread_List_Node {
    TThread_List_Node* prev;
    TThread_List_Node* next;
    TThread_Struct* thread;
};

constexpr uint32_t Invalid_TID = static_cast<uint32_t>(-1);

class CThread_Manager {
  private:
    // seznam vsech SCHEDULOVATELNYCH vlaken (zombie vlakna se z nej sama odmazou)
    TThread_List_Node* mThread_List_Head;
    // zalozi prazdne vlakno (alokuje pamet)
    inline TThread_Struct* Alloc_Thread_Struct();
    

  public:
    CThread_Manager();
    // TODO: tady uklidit

    // Vytvori prvni vlakno procesu, nahraje kod do pameti
    uint32_t Create_First_Thread(TProcess_Struct* process, unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system);
    // Vytvori vlakno dle volajiciho rodice
    uint32_t Create_Thread(TProcess_Struct* process, uint32_t func_addr);
    // prevede dany thread do runnable, nastavi notified
    bool Notify_Thread(TThread_Struct* thread);

    TThread_List_Node* Get_Thread_List_Head();

    // softwarova preruseni pro process facility
    void Handle_Thread_SWI(NSWI_Thread_Service svc_idx, uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target);
};

extern CThread_Manager sThread_Manager;
