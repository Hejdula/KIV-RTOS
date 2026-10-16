#pragma once

// vycet stavu vlakna
enum class NThread_State {
    New,                // nove - zatim nenaplanovane, neinicializovane, stav na jiny se zmeni jeste behem vytvareni
    Runnable,           // pripravene k naplanovani (uz mohlo v minulosti bezet)
    Running,            // prave naplanovane
    Blocked,            // blokovane - genericky stav pro vlakno, ktere by sice mohl byt naplanovano, ale ceka na nejaky prostredek
                        // pro prakticke ucely se stav "Blokovane" deli jeste na podstavy, aby bylo jasne, na jaky prostredek se ceka (mutex, cteni z disku, jine I/O, ...)
    Interruptable_Sleep,// uspane vlakno - ceka az vyprsi nejaky casovy interval, popr. pokud ceka na podminkove promenne, tak ho muze probudit jeste notifikace
    Zombie,             // vlakno je ukonceno a ceka na to, az si nekdo precte jeho navratovy kod
};

#pragma pack(push, 1)

// kontext provadeni vlakna z pohledu CPU - v podstate ulozene registry
struct TCPU_Context {
    unsigned long lr;
    unsigned long sp;
    unsigned long pc;
    unsigned long ttbr0; // kazde vlakno ma svuj pointer na ttbr0, dedi ho od vlakna, ktere jej zalozilo, prvni vlakno ho dostane pri vytvoreni procesu
};

#pragma pack(pop)

struct TTCB_Entry {
    TCPU_Context cpu_context; // struct obsahujici stav klicovych registru a pointer na tabulku stranek
    unsigned int pid; // proces ID tasku, pod ktery thread spada
    unsigned int tid; // thread ID v ramci tasku
    NThread_State state; // stav vlakna pro ucely planovace
};