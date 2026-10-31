#include "process/process.h"
#include "process/thread.h"
#include <drivers/timer.h>
#include <memory/kernel_heap.h>
#include <memory/memmap.h>
#include <memory/mmu.h>
#include <memory/pages.h>
#include <memory/pt_alloc.h>
#include <process/elf_loader.h>
#include <process/process_manager.h>
#include <process/scheduler.h>
#include <process/thread_manager.h>

CThread_Manager sThread_Manager;

// "importovane" funkce z asm
extern "C" {
void user_process_bootstrap();
void system_process_bootstrap();
};

CThread_Manager::CThread_Manager() {};

inline TThread_Struct* CThread_Manager::Alloc_Thread_Struct() {
    auto* newThread = sKernelMem.Alloc<TThread_Struct>();
    return newThread;
}

uint32_t CThread_Manager::Create_First_Thread(TProcess_Struct* process, unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system) {
    if (process->threads[0] != nullptr) {
        return Invalid_TID; // pokud tam uz vlakno je tak jsme tuhle funkci zavolali omylem
    }

    TThread_List_Node* threadnode = sKernelMem.Alloc<TThread_List_Node>();
    if (!threadnode) {
        return Invalid_TID; // Allocation failed
    }
    threadnode->next = this->mThread_List_Head;
    threadnode->prev = nullptr;
    if (this->mThread_List_Head) {
        this->mThread_List_Head->prev = threadnode;
    }
    this->mThread_List_Head = threadnode;

    // alokujeme pamet pro vlakno
    process->threads[0] = this->Alloc_Thread_Struct();
    // ulozime odkaz na vlakno do seznamu vlaken
    threadnode->thread = process->threads[0];
    // defaultni a implicitni hodnoty pro vlakno
    process->threads[0]->tid = 0;
    process->threads[0]->state = NThread_State::New;
    process->threads[0]->sched_counter = process->sched_static_priority;
    process->threads[0]->is_multi_file_wait = false;
    process->threads[0]->deadline = Indefinite;
    process->threads[0]->notified_deadline = Indefinite;
    process->threads[0]->blocked_on_tid = Invalid_TID;
    process->threads[0]->process = process;

    // adpatace puvodniho kodu zakladani procesu
    // lr = co zacit vykonavat po bootstrapu, 0x8000 je misto, kam je relokovany v kazde nasi binarce symbol _start, tedy vstupni bod programu
    process->threads[0]->cpu_context.lr = 0x8000; // TODO: zjistit proc se tohle dela, vsak to o par radku niz zase prepisu tim z elf souboru

    // pc = "aktualni" kod k provadeni, tedy bootstrap procesu v jadre - lisi se fakticky jen tim, zda je do CPSR vlozen rezim uzivatelsky nebo systemovy,
    // ale teoreticky muzeme chtit pridat jeste dalsi veci specificke pro nejaky z rezimu
    process->threads[0]->cpu_context.pc =
        is_system ? reinterpret_cast<unsigned long>(&system_process_bootstrap) : reinterpret_cast<unsigned long>(&user_process_bootstrap);
    // sp = zasobnik, vzdy je relokovany na nejakou fixni pamet, pro nas je jednoduche volit ho o velikosti 1MB
    // pozn. zasobnik roste na druhou stranu, takze musime SP nastavit na konec stranky
    process->threads[0]->cpu_context.sp = Stack_Area_Virtual_Base + Stack_Guard_Size + Stack_Size;

    // alokujeme stranku pro kod a pro zasobnik
    uint32_t code_page_phys = static_cast<unsigned long>(sPage_Manager.Alloc_Page()) - mem::MemoryVirtualBase;
    uint32_t stack_page_phys = static_cast<unsigned long>(sPage_Manager.Alloc_Page()) - mem::MemoryVirtualBase;
    process->page_count += 2;

    // alokujeme tabulku stranek procesu z poolu
    uint32_t* pt = sPT_Alloc.Alloc();
    process->page_table = pt;

    // zkopirujeme zakladni kernelovou tabulku - to proto, aby zustalo mapovani 0xFxxxxxxx a 0xCxxxxxxx adres (kod a data) a mohli jsme vykonavat
    // napr. obsluhu systemoveho volani beze zmeny adresniho prostoru
    // pozn.: tohle kopirovani se da odbourat korektnim pouzitim TTBR1 - pak by TTBR1 prekladal vse napr. od 0x8xxxxxxx vyse a TTBR0 by zustal pro spodni cast
    // adr. rozsahu
    //        pro jednoduchost ted nechme takto, dvojice bazovych registru pro tabulky stranek jsou spise specialita ARMu a pribuznych procesoru
    copy_kernel_page_table_to(pt);

    // namapujeme kodovou stranku na zacatek virtualni pameti
    map_memory(pt, code_page_phys, 0x00000000);
    // namapujeme zasobnik na virtualni adresu prostory pro stacky,
    // posunuty zacatek o guard jelikoz stack roste dolu a guard je tedy na nizsi adrese pred nim.
    map_memory(pt, stack_page_phys, Stack_Area_Virtual_Base + Stack_Guard_Size);

    // nakopirujeme kod do kodove stranky, vysledkem nacitani je i PC vstupniho bodu procesu
    const uint32_t elf_pc =
        CELF_Loader::Load_ELF32_Image(elf_file_data, reinterpret_cast<unsigned char*>(code_page_phys) + mem::MemoryVirtualBase, mem::PageSize);
    if (elf_pc == CELF_Loader::Invalid_Entry_Point) {
        // pokud se nepodarilo nacteni, uvolnime alokovane zdroje a vratime chybu
        sPT_Alloc.Free(pt);
        sPage_Manager.Free_Page(code_page_phys + mem::MemoryVirtualBase);
        sPage_Manager.Free_Page(stack_page_phys + mem::MemoryVirtualBase);
        sKernelMem.Free(process->threads[0]);
        process->page_count -= 2;
        return Invalid_TID;
    }

    // nastavime PC vlakna na vstupni bod programu, ktery jsme nacetli z ELF souboru
    process->threads[0]->cpu_context.lr = elf_pc;

    // nastavime ulozene TTBR0 procesu - tady musi byt fyzicka adresa, proto odecitame bazi (vsechny fyzicke adresy jsou v kernelovem modu mapovany na
    // "0xC0000000 + offset")
    process->ttbr0 = (reinterpret_cast<unsigned long>(pt) - mem::MemoryVirtualBase) | TTBR_Flags::Inner_Cacheable | TTBR_Flags::Shared;
    process->threads[0]->cpu_context.ttbr0 = process->ttbr0;
    process->thread_count = 1;

    return process->threads[0]->tid;
}

uint32_t CThread_Manager::Create_Thread(TProcess_Struct* process, uint32_t func_addr) {
    uint32_t TID = Invalid_TID;
    { // first fit available TID
        for (uint32_t i = 0; i < Max_Process_Threads; ++i) {
            if (process->threads[i] != nullptr) {
                TID = i;
                break;
            }
        }

        if (TID == Invalid_TID) {
            return Invalid_TID;
        }
    }

    TThread_List_Node* threadnode = sKernelMem.Alloc<TThread_List_Node>();
    if (!threadnode) {
        return Invalid_TID; // Allocation failed
    }

    TThread_Struct* new_thread = Alloc_Thread_Struct();
    if (!new_thread) {
        sKernelMem.Free(threadnode);
        return Invalid_TID; // Allocation failed
    }

    // zaradit do listu TThread_Struct
    threadnode->next = this->mThread_List_Head;
    threadnode->prev = nullptr;
    if (this->mThread_List_Head) {
        this->mThread_List_Head->prev = threadnode;
    }
    this->mThread_List_Head = threadnode;

    threadnode->thread = new_thread;
    process->threads[TID] = new_thread;

    uint32_t stack_page_phys = static_cast<unsigned long>(sPage_Manager.Alloc_Page()) - mem::MemoryVirtualBase;

    // kde ve pameti bude stack, pred nim jsou stacky a guard vlaken s nizsim TIO a guard page tohoto stacku
    uint32_t stack_page_virtual = (Stack_Area_Virtual_Base + Stack_Guard_Size + TID * (Stack_Size + Stack_Guard_Size));
    // namapujeme stack vlakna do tabulky stranek
    map_memory(process->page_table, stack_page_phys, stack_page_virtual);

    // *new_thread = TThread_Struct{}; // this needs memset to be defined, would be nice for zeroing the structs
    new_thread->cpu_context = TCPU_Context{.lr = 0, // TODO: tady bude adresa thread-exit handleru
                                           .sp = stack_page_virtual + Stack_Size,
                                           .pc = func_addr,
                                           .ttbr0 = process->ttbr0};
    new_thread->process = process;
    new_thread->tid = TID;
    new_thread->state = NThread_State::New;
    new_thread->sched_counter = process->sched_static_priority;
    new_thread->is_multi_file_wait = false;
    new_thread->deadline = Indefinite;
    new_thread->notified_deadline = Indefinite;
    new_thread->blocked_on_tid = Invalid_TID;

    process->thread_count++;

    return TID;
};

bool CThread_Manager::Notify_Thread(TThread_Struct* thread) {
    if (!thread) {
        return false;
    }

    // ve slozitejsich planovacich by tady mohl byt treba i presun do jine fronty procesu, apod.
    thread->state = NThread_State::Runnable;

    // pokud byla nastavena "budouci" deadline, nastavime skutecnou deadline
    if (thread->notified_deadline != Deadline_Unchanged) {
        thread->deadline = sTimer.Get_Tick_Count() + thread->notified_deadline;
    }
    // pozor: tady nesmime mazat budouci deadline - co kdyby byl proces probuzen falesne? (stolen wakeup)
    //        v kernelu by se to sice stavat uplne nemelo, ale co kdybychom se nekde upsali

    return true;
}

// TODO: tykaji se vsechny procesu? Terminate, Get_PID se procesu tyka, ale sleep/Yield je k vlaknu - Hejdula
// ANSWER: Terminate zabije proces ale z POV vlakna, GETPID muze udelat kazdy vlakno samo o sobe, za me tohle vsechno patri do thread - Adam
// TODO: pridat exit_thread, pridat join

void CThread_Manager::Handle_Thread_SWI(NSWI_Thread_Service svc_idx, uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target) {
    // TODO: signalizace chyby
    TThread_Struct* cur_thread = sScheduler.Get_Current_Thread();
    if (!cur_thread) {
        return;
    }

    switch (svc_idx) {
        case NSWI_Thread_Service::Get_PID:
            target.r0 = cur_thread->process->pid;
            break;

        case NSWI_Thread_Service::Get_TID:
            target.r0 = cur_thread->tid;
            break;

        case NSWI_Thread_Service::Terminate:
            // TODO: je treba korekne ukoncit vsechny vlakna, soubory, heap atd a nastavit process jako zombie
            // mCurrent_Task_Node->task->sched_counter = 1;
            // mCurrent_Task_Node->task->state = NTask_State::Zombie;
            // mCurrent_Task_Node->task->exit_code = r0;
            // Close_All_Files(mCurrent_Task_Node->task);
            sScheduler.Schedule();
            break;

        case NSWI_Thread_Service::Yield:
            sScheduler.Schedule();
            break;

        case NSWI_Thread_Service::Sleep:
            cur_thread->sched_counter = 1;
            cur_thread->state = NThread_State::Interruptable_Sleep;
            cur_thread->notified_deadline = r1;
            cur_thread->sleep_timer = ((r0 == Indefinite) ? r0 : sTimer.Get_Tick_Count() + r0);
            sScheduler.Schedule();
            break;

        case NSWI_Thread_Service::Get_Sched_Info:
            sScheduler.Get_Scheduler_Info(static_cast<NGet_Sched_Info_Type>(r0), reinterpret_cast<void*>(r1));
            break;

        case NSWI_Thread_Service::Deadline: {
            NDeadline_Subservice dtype = static_cast<NDeadline_Subservice>(r0);
            uint32_t* r1ptr = reinterpret_cast<uint32_t*>(r1);
            switch (dtype) {
                case NDeadline_Subservice::Set_Relative:
                    cur_thread->deadline = ((*r1ptr == Indefinite) ? Indefinite : (sTimer.Get_Tick_Count() + *r1ptr));
                    cur_thread->notified_deadline = Indefinite;
                    break;
                case NDeadline_Subservice::Get_Remaining:
                    // dTODO: co kdybychom prosvihli deadline?
                    *r1ptr = (cur_thread->deadline == Indefinite) ? Indefinite : cur_thread->deadline - sTimer.Get_Tick_Count();
                    break;
            }
            break;
        }

        // TODO: implementovat
        case NSWI_Thread_Service::Thread:
            if (cur_thread->process->thread_count >= Max_Process_Threads) {
                target.r0 = Invalid_TID;
                break;
            }

            target.r0 = Create_Thread(cur_thread->process, r0);
            // v r0 vracime TID, muze byt invalid, thread se pak nepodarilo vytvorit
            sScheduler.Schedule();
            break;

        case NSWI_Thread_Service::Join: {
            auto* target_thread = cur_thread->process->threads[r0]; // vyzvedneme si ukazatel na vlakno, ktere joinujeme

            if (target_thread->state != NThread_State::Zombie) {    // pokud dane vlakno jeste nedobehlo
                cur_thread->state = NThread_State::Blocked_on_Join; // tak se zablokujeme
                cur_thread->blocked_on_tid = target_thread->tid;    // a ulozime, na kom jsme se zablokovali
                sScheduler.Schedule();
            }

            // ted uz vlakno zombie urcite je
            cur_thread->blocked_on_tid = Invalid_TID; // takze nastavime ze na nikoho necekame
            target.r0 = target_thread->exit_code;

            bool others_waiting = false;
            for (TThread_Struct* thread :
                 cur_thread->process
                     ->threads) { // projedeme vsechny vlakna, ktera jsou odblokovana nad timto vlaknem, ale jeste si nevyzvedla navratovou hodnotu
                if (thread->state == NThread_State::Runnable && thread->blocked_on_tid == target_thread->tid) { //
                    others_waiting = true; // pokud nekdo takovy je, tak nebudeme delat cleanup
                    break;
                }
            }
            if (!others_waiting) { // pokud jsme posledni, kdo si vybira navratovou hodnotu, tak uklidime TCB
                sKernelMem.Free(cur_thread->process->threads[target_thread->tid]);
                cur_thread->process->threads[target_thread->tid] =
                    nullptr; // a nastavime v procesu ze dany handle je volny - TODO: tohle mozna bude v thread_manageru
                cur_thread->process->thread_count--;
            }

            break;
        }

        case NSWI_Thread_Service::Exit_Thread:
            cur_thread->state = NThread_State::Zombie; // deklarujeme ze je zombie
            cur_thread->exit_code = r0;                // natavime exit code

            // vypoctu si adresu stackove stranky v process-prostoru. Jelikoz jsme sem skocili z programu (volanim), tak stack vlakna nebude nikdy prazdny,
            // nemuze se stat ze by tohle vyhodilo adresu stranky o jedna vyssi
            uint32_t stack_page_virt = (cur_thread->cpu_context.sp / PT_Region_Size) * PT_Region_Size;
            // vypoctu si pointer na page table a odmapuju stack
            uint32_t* pt =
                reinterpret_cast<uint32_t*>((cur_thread->cpu_context.ttbr0 & ~(TTBR_Flags::Inner_Cacheable | TTBR_Flags::Shared)) + mem::MemoryVirtualBase);
            // vypoctu si adresu stackove stranky v kernel prostoru
            const uint32_t stack_page_kernel_virt = (pt[stack_page_virt / PT_Region_Size] & 0xFFF00000) + mem::MemoryVirtualBase;
            // odmapovavam z page table processu - potrebuji adresu v process prostoru
            unmap_memory(pt, stack_page_virt);
            // dealokuju stranku, kde byl stack - potrebuju adresu v kernel prostoru
            sPage_Manager.Free_Page(stack_page_kernel_virt);
            cur_thread->process->page_count--;

            // musime se jeste odhlasit ze seznamu vlaken
            auto* list_node = mThread_List_Head;
            while (list_node->thread != cur_thread) {
                list_node = list_node->next;
                // ze v seznamu neni nepredpokladam, a kdyby nebyla tak mame praci hotovou
                if (!list_node) {
                    break;
                }
            }
            if (list_node) {
                // pokud ma predchudce tak normalni operace, jinak odebirame thread list head!
                if (list_node->prev) {
                    list_node->prev->next = list_node->next;
                } else {
                    mThread_List_Head = list_node->next;
                }
                if (list_node->next) {
                    list_node->next->prev = list_node->prev;
                }
                sKernelMem.Free(list_node);
            }

            // Page table dealokuje terminate

            for (TThread_Struct* thread : cur_thread->process->threads) { // projedeme vsechny vlakna, ktera jsou zablokovana nad "my.join()"
                if (thread->state == NThread_State::Blocked_on_Join && thread->blocked_on_tid == cur_thread->tid) {
                    thread->state = NThread_State::Runnable; // a odblokujeme je
                }
            }

            // TODO: pokud jsem posledni thread procesu tak volam terminate?

            sScheduler.Schedule();

            break;
    }
}

TThread_List_Node* CThread_Manager::Get_Thread_List_Head() { return this->mThread_List_Head; };
