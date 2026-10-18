#pragma once

#include <hal/intdef.h>

#include "process.h"
#include "thread.h"
#include "swi.h"
#include <fs/filesystem.h>
#include <stdstring.h>

class CThread_Manager
{
private:
    // zalozi prazdne vlakno (alokuje pamet)
    TThread_Struct* Create_Thread();
public:
    CThread_Manager(/* args */);
    ~CThread_Manager();

    // Vytvori prvni vlakno procesu, nahraje kod do pameti
    TThread_Struct* Create_First_Thread(unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system);
    // Vytvori vlakno dle volajiciho rodice
    TThread_Struct* Fork_Thread(TThread_Struct* parent);
    //ziska TID prave naplanovaneho vlakna (pro systemove volani )
    uint32_t Get_Current_TID();


};





