#include <process/thread_manager.h>

// "importovane" funkce z asm
extern "C" {
    void user_process_bootstrap();
    void system_process_bootstrap();
};

inline TThread_Struct *CThread_Manager::Create_Thread()
{
    auto* newThread = sKernelMem.Alloc<TThread_Struct>();

}

uint32_t CThread_Manager::Create_First_Thread(TProcess_Struct* process, unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system)
{
    if(process->threads[0] != nullptr) {
        return Invalid_TID; //pokud tam uz vlakno je tak jsme tuhle funkci zavolali omylem
    }

    // alokujeme pamet pro vlakno
    process->threads[0] = this->Create_Thread();
    // defaultni a implicitni hodnoty pro vlakno
    process->threads[0]->tid = 0; 
    process->threads[0]->state = NThread_State::New;
    process->threads[0]->sched_counter = process->sched_static_priority;
    process->threads[0]->is_multi_file_wait = false;
    process->threads[0]->deadline = Indefinite;
    process->threads[0]->notified_deadline = Indefinite;

    // adpatace puvodniho kodu zakladani procesu
    // lr = co zacit vykonavat po bootstrapu, 0x8000 je misto, kam je relokovany v kazde nasi binarce symbol _start, tedy vstupni bod programu
    process->threads[0]->cpu_context.lr = 0x8000; // TODO: zjistit proc se tohle dela, vsak to o par radku niz zase prepisu tim z elf souboru

    // pc = "aktualni" kod k provadeni, tedy bootstrap procesu v jadre - lisi se fakticky jen tim, zda je do CPSR vlozen rezim uzivatelsky nebo systemovy,
    // ale teoreticky muzeme chtit pridat jeste dalsi veci specificke pro nejaky z rezimu
    process->threads[0]->cpu_context.pc = is_system ? reinterpret_cast<unsigned long>(&system_process_bootstrap) : reinterpret_cast<unsigned long>(&user_process_bootstrap);
    // sp = zasobnik, vzdy je relokovany na nejakou fixni pamet, pro nas je jednoduche volit ho o velikosti 1MB
    // pozn. zasobnik roste na druhou stranu, takze musime SP nastavit na konec stranky
    process->threads[0]->cpu_context.sp = 0x90000000 + mem::PageSize;

    // alokujeme stranku pro kod a pro zasobnik
    uint32_t code_page_phys = static_cast<unsigned long>(sPage_Manager.Alloc_Page()) - mem::MemoryVirtualBase;
    uint32_t stack_page_phys = static_cast<unsigned long>(sPage_Manager.Alloc_Page()) - mem::MemoryVirtualBase;
    process->page_count += 2;

    // alokujeme tabulku stranek procesu z poolu
    uint32_t* pt = sPT_Alloc.Alloc();

    // zkopirujeme zakladni kernelovou tabulku - to proto, aby zustalo mapovani 0xFxxxxxxx a 0xCxxxxxxx adres (kod a data) a mohli jsme vykonavat
    // napr. obsluhu systemoveho volani beze zmeny adresniho prostoru
    // pozn.: tohle kopirovani se da odbourat korektnim pouzitim TTBR1 - pak by TTBR1 prekladal vse napr. od 0x8xxxxxxx vyse a TTBR0 by zustal pro spodni cast adr. rozsahu
    //        pro jednoduchost ted nechme takto, dvojice bazovych registru pro tabulky stranek jsou spise specialita ARMu a pribuznych procesoru
    copy_kernel_page_table_to(pt);

    // namapujeme kodovou stranku na zacatek virtualni pameti
    map_memory(pt, code_page_phys, 0x00000000);
    // namapujeme zasobnik na 0x90000000
    map_memory(pt, stack_page_phys, 0x90000000);

    // nakopirujeme kod do kodove stranky, vysledkem nacitani je i PC vstupniho bodu procesu
    const uint32_t elf_pc = CELF_Loader::Load_ELF32_Image(elf_file_data, reinterpret_cast<unsigned char*>(code_page_phys) + mem::MemoryVirtualBase, mem::PageSize);
    if (elf_pc == CELF_Loader::Invalid_Entry_Point) {
        // pokud se nepodarilo nacteni, uvolnime alokovane zdroje a vratime chybu
        sPT_Alloc.Free(pt);
        sPage_Manager.Free_Page(code_page_phys + mem::MemoryVirtualBase);
        sPage_Manager.Free_Page(stack_page_phys + mem::MemoryVirtualBase);
        sKernelMem.Free(process->threads[0]);
        return Invalid_TID;
    }

    // nastavime PC vlakna na vstupni bod programu, ktery jsme nacetli z ELF souboru
    process->threads[0]->cpu_context.lr = elf_pc;

    // nastavime ulozene TTBR0 procesu - tady musi byt fyzicka adresa, proto odecitame bazi (vsechny fyzicke adresy jsou v kernelovem modu mapovany na "0xC0000000 + offset")
    process->threads[0]->cpu_context.ttbr0 = (reinterpret_cast<unsigned long>(pt) - mem::MemoryVirtualBase)
        | TTBR_Flags::Inner_Cacheable
        | TTBR_Flags::Shared;
    

    return process->threads[0]->tid;
}
