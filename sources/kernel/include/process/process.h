#pragma once

#include "memory/memmap.h"
#include <hal/intdef.h>

// namespace proc{
//     // TODO: pro rozsiritelnost a prehlednost by davalo smysl mit globals ve scope
// }

// maximalni pocet otevrenych souboru
constexpr uint32_t Max_Process_Opened_Files = 16;
// maximalni pocet vlaken
constexpr uint32_t Max_Process_Threads = 16;
// hodnota, ktera znamena, ze proces nema casovy limit na vzbuzeni / deadline
constexpr uint32_t Indefinite = static_cast<uint32_t>(-1);
// hodnota, ktera znamena, ze procesu se nema menit deadline
constexpr uint32_t Deadline_Unchanged = static_cast<uint32_t>(-2);
// velikost stacku
constexpr uint32_t Stack_Size = mem::PageSize;
// velikost nemapovaneho mista pod stackem pro hlidani preteceni
constexpr uint32_t Stack_Guard_Size = mem::PageSize;
// virtualni adresa na ktere zacinaji stacky vlaken procesu
constexpr uint32_t Stack_Area_Virtual_Base = 0x90000000;
// celkova velikost oblasti pouzita na mapovani stacku
constexpr uint32_t Stacks_Area_Size = (Stack_Size + Stack_Guard_Size) * Max_Process_Threads;

class IFile;
struct TThread_Struct;

// struktura procesu (tasku, ...)
struct TProcess_Struct {
    uint32_t pid;                                   // ID procesu, kladne nenulove cislo
    uint32_t sched_static_priority;                 // staticka priorita procesu (dana pri jeho vytvareni)
    IFile* opened_files[Max_Process_Opened_Files];  // otevrene soubory; index je zaroven handle, souborove veci jsou sdilene mezi vlakny, pro ne musime sahat do     
    int exit_code;                                  // navratovy kod procesu; nastaveny pri terminaci posledniho vlakna
    uint32_t* page_table;                           // adresa tabulky stranek procesu ve virtualnim prostoru kernelu
    unsigned long ttbr0;                            // obsah registru pro nastaveni tabulky stranek procesu
    uint32_t page_count;                            // pocet alokovanych stranek
    uint32_t thread_count;                          // pocet ZIVYCH vlaken - posledni vlakno nastavuje exit code, kdyz se rovna Max_Process_Threads tak TODO vyhodit chybu
    TThread_Struct* threads[Max_Process_Threads];   // seznam vsech vlaken procesu
};
