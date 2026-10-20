#pragma once

#include <hal/intdef.h>

// maximalni pocet otevrenych souboru
constexpr uint32_t Max_Process_Opened_Files = 16;
// maximalni pocet vlaken
constexpr uint32_t Max_Process_Threads = 16;

// hodnota, ktera znamena, ze proces nema casovy limit na vzbuzeni / deadline
constexpr uint32_t Indefinite = static_cast<uint32_t>(-1);

// hodnota, ktera znamena, ze procesu se nema menit deadline
constexpr uint32_t Deadline_Unchanged = static_cast<uint32_t>(-2);



#pragma pack(push, 1)

// TODO tohle zmizi
// kontext provadeni procesu z pohledu CPU - v podstate ulozene registry
/*struct TCPU_Context {
    unsigned long lr;
    unsigned long sp;
    unsigned long pc;
    unsigned long ttbr0; 
};*/

#pragma pack(pop)

class IFile;
struct TThread_Struct;

// struktura procesu (tasku, ...)
struct TProcess_Struct {
    //TCPU_Context cpu_context;                   // ulozeny kontext procesoru - misto toho v thread
    unsigned int pid;                           // ID procesu, kladne nenulove cislo
    //NTask_State state;                          // stav procesu - misto toho v thread
    //unsigned int sched_counter;                 // pocitadlo - misto toho v thread
    unsigned int sched_static_priority;         // staticka priorita procesu (dana pri jeho vytvareni)
    IFile* opened_files[Max_Process_Opened_Files];  // otevrene soubory; index je zaroven handle, souborove veci jsou sdilene mezi vlakny, pro ne musime sahat do procesu
    //uint32_t multi_wait_fd_bitmap;              // bitmapa souboru, na ktere proces ceka pri WaitAll - cekani na vice souboru najednou
    //bool is_multi_file_wait;                    // indikuje, ze proces ceka na vice souboru najednou (WaitAll)
    //uint32_t multi_wait_ready_fd;               // soubor, ktery byl prvni pripraven k operaci pri cekani na vice souboru (WaitAll)
    int exit_code;                              // navratovy kod procesu; nastaveny pri terminaci posledniho vlakna
    //uint32_t sleep_timer;                       // casovac pro uspane procesy - jakmile systemovy citac prekroci tuto hodnotu, proces je odblokovan
                                                // maximalni podporovany rozsah cekani je 0x7FFFFFFF, jelikoz muze citac pretect; diference je vzdy pocitana i s moznosti preteceni
    //uint32_t deadline;                          // deadline dokonceni tasku
    //uint32_t notified_deadline;                 // deadline nastavena po nasledujicim probuzeni procesu
    uint32_t page_count;                        // pocet alokovanych stranek
    uint32_t thread_count;                      // pocet ZIVYCH vlaken - posledni vlakno nastavuje exit code, kdyz se rovna Max_Process_Threads tak TODO vyhodit chybu
    TThread_Struct* threads[Max_Process_Threads];  //seznam vsech vlaken procesu
};
