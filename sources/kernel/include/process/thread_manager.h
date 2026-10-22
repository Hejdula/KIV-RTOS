#pragma once
#include <hal/intdef.h>
#include <memory/kernel_heap.h>
#include <memory/pages.h>
#include <memory/memmap.h>
#include <memory/mmu.h>
#include <memory/pt_alloc.h>
#include <process/elf_loader.h>


#include "process.h"
#include "thread.h"
#include "swi.h"
#include <fs/filesystem.h>
#include <stdstring.h>

class CThread_Manager;

extern CThread_Manager sThread_Manager;
constexpr uint32_t Invalid_TID = static_cast<uint32_t>(-1);

class CThread_Manager
{
private:
    // zalozi prazdne vlakno (alokuje pamet)
    inline TThread_Struct* Create_Thread();
public:
    CThread_Manager() = default;
    ~CThread_Manager() = default;

    // Vytvori prvni vlakno procesu, nahraje kod do pameti
    uint32_t Create_First_Thread(TProcess_Struct* process, unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system);
    // Vytvori vlakno dle volajiciho rodice
    uint32_t Fork_Thread(TThread_Struct* parent);
    //ziska TID prave naplanovaneho vlakna (pro systemove volani )
    uint32_t Get_Current_TID();


};





