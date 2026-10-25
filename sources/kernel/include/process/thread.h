#pragma once

#include <hal/intdef.h>
#include <process/process.h>

// vycet stavu vlakna
enum class NThread_State {
    New,                // nove - zatim nenaplanovane, neinicializovane, stav na jiny se zmeni jeste behem vytvareni
    Runnable,           // pripravene k naplanovani (uz mohlo v minulosti bezet)
    Running,            // prave naplanovane
    Blocked,            // blokovane - genericky stav pro vlakno, ktere by sice mohl byt naplanovano, ale ceka na nejaky prostredek
                        // pro prakticke ucely se stav "Blokovane" deli jeste na podstavy, aby bylo jasne, na jaky prostredek se ceka (mutex, cteni z disku, jine I/O, ...)
    Interruptable_Sleep,// uspane vlakno - ceka az vyprsi nejaky casovy interval, popr. pokud ceka na podminkove promenne, tak ho muze probudit jeste notifikace
    Zombie,             // vlakno je ukonceno a ceka na to, az si nekdo precte jeho navratovy kod
    Blocked_on_Join,    // vlakno zavolalo join na jine vlakno a ceka nez dane vlakno dokonci praci a ukonci se
};

#pragma pack(push, 1)


// kontext provadeni vlakna z pohledu CPU - v podstate ulozene registry
struct TCPU_Context {
    unsigned long lr;
    unsigned long sp;
    unsigned long pc;
    unsigned long ttbr0; // kazde vlakno ma svuj pointer na ttbr0, pri zakladani vlakna je do nej vlozena tabulka stranek. TODO: dle implementace vyjasnit comment
};

#pragma pack(pop)

struct TThread_Struct {
    TCPU_Context cpu_context;                   // struct obsahujici stav klicovych registru a pointer na tabulku stranek
    TProcess_Struct* process;                   // proces, pod ktery thread spada
    unsigned int tid;                           // thread ID v ramci procesu - TODO zkontrolovat, jestli to k necemu je
    NThread_State state;                        // stav vlakna pro ucely planovace
    unsigned int sched_counter;                 // pocitadlo - jakmile je vlakno naplanovano, zkopiruje se do nej priorita procesu a kazdy tik casovace snizuje toto cislo o 1; na 0 se preplanuje na jine vlakno
    uint32_t multi_wait_fd_bitmap;                         // bitmapa souboru, na ktere vlakno ceka pri WaitAll - cekani na vice souboru najednou
    bool is_multi_file_wait;                    // indikuje, ze proces ceka na vice souboru najednou (WaitAll)
    uint32_t multi_wait_ready_fd;               // soubor, ktery byl prvni pripraven k operaci pri cekani na vice souboru (WaitAll)
    int exit_code;                              // navratovy kod procesu; nastaveny pri volani terminate nebo pri vyvolanem data/prefetch abortu a jinych - TODO misto tohodle musime brat navratovy kod hlavniho vlakna
    uint32_t sleep_timer;                       // casovac pro uspana vlakna - jakmile systemovy citac prekroci tuto hodnotu, vlakno je odblokovano
                                                // maximalni podporovany rozsah cekani je 0x7FFFFFFF, jelikoz muze citac pretect; diference je vzdy pocitana i s moznosti preteceni
    uint32_t deadline;                          // soucasny deadline dokonceni tasku
    uint32_t notified_deadline;                 // deadline nastavena po nasledujicim probuzeni vlakna (kolik casu ma vlakno po probuzeni po cekani na neco)
    unsigned int blocked_on_tid;                // TID vlakna, na ktere ceka behem joinu na 
};
