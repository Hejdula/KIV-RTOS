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

    // odebere uzel z listu vlaken
    void Unlink_Thread_Node(TThread_List_Node* node);
    // allocates and links a thread node to the thread list
    TThread_List_Node* Link_Thread_To_List(TThread_Struct *thread);

  public:
    CThread_Manager();
    // TODO: tady uklidit

    // Vytvori prvni vlakno procesu, nahraje kod do pameti
    uint32_t Create_First_Thread(TProcess_Struct* process, unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system);

    // Vytvori vlakno dle volajiciho rodice
    uint32_t Create_Thread(TProcess_Struct* process, uint32_t func_addr);

    // prevede dany thread do runnable, nastavi notified
    bool Notify_Thread(TThread_Struct* thread);

    // notifikuje dane vlakno
    bool Notify_Thread(PIDTID pidtid, IFile* notifier_file);
    
    // vrati ukazatel na strukturu vlakna se zadanym PIDTID, nebo nullptr, pokud vlakno neexistuje
    TThread_Struct* Get_Thread_By_PIDTID(PIDTID pidtid) const;

    // softwarova preruseni pro process facility
    void Handle_Thread_SWI(NSWI_Thread_Service svc_idx, uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target);

    static PIDTID GetPIDTID(const TThread_Struct* thread);
    // getter uvodniho uzlu listu vlaken
    TThread_List_Node* Get_Thread_List_Head() const;

    // nastavi status vlakna jako zombie a jeho exit_code, pripadne ukonci proces pokud to je hlavni vlakno
    void Handle_Exit_Thread(TThread_Struct* thread, uint32_t exit_code);

    // uvolni zdroje souvisejici s vlaknem, uzel v listu, zasobnik, nelze volat nad hlavnim vlaknem
    void Terminate_Thread(TProcess_Struct* proc, TThread_List_Node* thread_node);
};

extern CThread_Manager sThread_Manager;
