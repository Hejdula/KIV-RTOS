#include "process/process.h"
#include "process/thread.h"
#include "stdfile.h"
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

    Link_Thread_Node(threadnode);

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
    process->threads[0]->multi_wait_fd_bitmap = 0;

    // adpatace puvodniho kodu zakladani procesu
    // lr = co zacit vykonavat po bootstrapu, 0x8000 je misto, kam je relokovany v kazde nasi binarce symbol _start, tedy vstupni bod programu
    process->threads[0]->cpu_context.lr = 0x8000; // TODO: zjistit proc se tohle dela, vsak to o par radku niz zase prepisu tim z elf souboru

    process->is_system = is_system;
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
        sPage_Manager.Free_Page(code_page_phys);
        sPage_Manager.Free_Page(stack_page_phys);
        sKernelMem.Free(process->threads[0]);
        Unlink_Thread_Node(threadnode);
        sKernelMem.Free(threadnode);
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

uint32_t CThread_Manager::Create_Thread(TProcess_Struct* process, uint32_t func_addr, uint32_t arg, uint32_t userspace_thread_wrapper) {
    uint32_t TID = Invalid_TID;
    { // first fit available TID
        for (uint32_t i = 0; i < Max_Process_Threads; ++i) {
            if (process->threads[i] == nullptr) {
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

    uint32_t* kernel_stack_top = reinterpret_cast<uint32_t*>(stack_page_phys + mem::MemoryVirtualBase + Stack_Size);

    *(--kernel_stack_top) = reinterpret_cast<uint32_t>(arg); 
    *(--kernel_stack_top) = reinterpret_cast<uint32_t>(func_addr);

    uint32_t stack_offset = reinterpret_cast<uint32_t>(kernel_stack_top) - (stack_page_phys + mem::MemoryVirtualBase);

    // *new_thread = TThread_Struct{}; // this needs memset to be defined, would be nice for zeroing the structs
    new_thread->cpu_context = TCPU_Context{.lr = userspace_thread_wrapper,
                                           .sp = stack_page_virtual + stack_offset,
                                           .pc = reinterpret_cast<uint32_t>(process->is_system ? &system_process_bootstrap : &user_process_bootstrap),
                                           .ttbr0 = process->ttbr0};
    new_thread->process = process;
    new_thread->tid = TID;
    new_thread->state = NThread_State::New;
    new_thread->sched_counter = process->sched_static_priority;
    new_thread->is_multi_file_wait = false;
    new_thread->multi_wait_ready_fd = 0;
    new_thread->deadline = Indefinite;
    new_thread->notified_deadline = Indefinite;
    new_thread->blocked_on_tid = Invalid_TID;

    process->thread_count++;

    return TID;
};

void CThread_Manager::Handle_Exit_Thread(TThread_Struct* cur_thread, uint32_t exit_code) {
    cur_thread->state = NThread_State::Zombie; // deklarujeme ze je zombie
    cur_thread->exit_code = exit_code;         // natavime exit code

    sScheduler.Sign_Up_For_Deferred_Cleanup(cur_thread->tid == 0, false);

    for (TThread_Struct* thread : cur_thread->process->threads) { // projedeme vsechny vlakna, ktera jsou zablokovana nad "my.join()"
        if (thread->state == NThread_State::Blocked_on_Join && thread->blocked_on_tid == cur_thread->tid) {
            thread->state = NThread_State::Runnable; // a odblokujeme je
        }
    }

    sScheduler.Schedule();
}

void CThread_Manager::Free_Thread_Resources(TProcess_Struct *proc, TThread_List_Node *thread_node){
    if(!proc || thread_node){
        return;
    }

    // free stack page
    uint32_t stack_page_virt = (Stack_Area_Virtual_Base + Stack_Guard_Size + (Stack_Size + Stack_Guard_Size) * thread_node->thread->tid);
    uint32_t stack_page_phys = (proc->page_table[stack_page_virt / PT_Region_Size] & 0xFFF00000);

    sPage_Manager.Free_Page(stack_page_phys);

    proc->page_count--;
    
    sKernelMem.Free(thread_node->thread);
    Unlink_Thread_Node(thread_node);
    sKernelMem.Free(thread_node);
}

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

bool CThread_Manager::Notify_Thread(PIDTID pidtid, IFile* notifier_file) {
    auto* thread = Get_Thread_By_PIDTID(pidtid);
    if (!thread) {
        return false;
    }

    // najdeme index otevreneho souboru
    uint32_t handle_index = 0;
    for (handle_index = 0; handle_index < Max_Process_Opened_Files; ++handle_index) {
        if (thread->process->opened_files[handle_index] == notifier_file) {
            break;
        }
    }
    if (handle_index == Max_Process_Opened_Files) {
        // tohle by nemelo nastat pokud kernel funguje spravne
        return false;
    }
    bool notified = false;

    if (thread->multi_wait_fd_bitmap & 1 << handle_index) {
        if (thread->is_multi_file_wait) {
            thread->multi_wait_ready_fd = handle_index;
        }
        notified |= sThread_Manager.Notify_Thread(thread);
    }
    if (!thread->is_multi_file_wait) { // pokud vlakno neceka na vice souboru tak ho tenhle MUSI probudit - nikdo jiny to neudela
        notified |= sThread_Manager.Notify_Thread(thread);
    }

    return notified;
}

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
            sProcessMgr.Terminate_Process(cur_thread->process);
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

        case NSWI_Thread_Service::Thread:
            if (cur_thread->process->thread_count >= Max_Process_Threads) {
                target.r0 = Invalid_TID;
                break;
            }

            target.r0 = Create_Thread(cur_thread->process, r0, r1, r2);
            // v r0 vracime TID, muze byt invalid, thread se pak nepodarilo vytvorit
            sScheduler.Schedule();
            break;

        case NSWI_Thread_Service::Join: {
            auto* target_thread = cur_thread->process->threads[r0]; // vyzvedneme si ukazat;el na vlakno, ktere joinujeme

            if (target_thread->state != NThread_State::Zombie) {    // pokud dane vlakno jeste nedobehlo
                cur_thread->state = NThread_State::Blocked_on_Join; // tak se zablokujeme
                cur_thread->blocked_on_tid = target_thread->tid;    // a ulozime, na kom jsme se zablokovali
                sScheduler.Schedule();
            }

            // ted uz vlakno zombie urcite je
            cur_thread->blocked_on_tid = Invalid_TID; // takze nastavime ze na nikoho necekame
            target.r0 = target_thread->exit_code;

            // dTODO: tohle prochazeni by se dalo nahradit pocitadlem kolik threadu na nej ceka, ale neni to nutne
            bool others_waiting = false;
            for (TThread_Struct* thread : cur_thread->process->threads) { // projedeme vsechny vlakna,
                // ktera jsou odblokovana nad timto vlaknem, ale jeste si nevyzvedla navratovou hodnotu
                if (thread->state == NThread_State::Runnable && thread->blocked_on_tid == target_thread->tid) { //
                    others_waiting = true; // pokud nekdo takovy je, tak nebudeme delat cleanup
                    break;
                }
            }
            if (!others_waiting) { // pokud jsme posledni, kdo si vybira navratovou hodnotu, tak uklidime TCB
                sKernelMem.Free(cur_thread->process->threads[target_thread->tid]);
                cur_thread->process->threads[target_thread->tid] = nullptr; // a nastavime v procesu ze dany handle je volny
                cur_thread->process->thread_count--;
            }

            break;
        }

        case NSWI_Thread_Service::Exit_Thread:
            // terminate process if exitting from main thread
            if (cur_thread->tid == 0) {
                sProcessMgr.Terminate_Process(cur_thread->process);
            } else {
                Handle_Exit_Thread(cur_thread, r0);
            }
            break;
    }
}

PIDTID CThread_Manager::GetPIDTID(const TThread_Struct* thread) {
    if (thread == nullptr) {
        return {Invalid_PID, Invalid_TID};
    }
    return {thread->process->pid, thread->tid};
}

TThread_List_Node* CThread_Manager::Get_Thread_List_Head() const { return this->mThread_List_Head; }
TThread_Struct* CThread_Manager::Get_Thread_By_PIDTID(PIDTID pidtid) const {
    if (pidtid.pid == Invalid_PID || pidtid.tid == Invalid_TID) {
        return nullptr;
    }

    TProcess_Struct* process = sProcessMgr.Get_Process_By_PID(pidtid.pid);
    if (process == nullptr) {
        return nullptr;
    }

    return process->threads[pidtid.tid];
    // nebo by slo prohledavat seznam vlaken, ale tohle bude se stoupajicim poctem vlaken pri konstantnim poctu procesu rychlejsi.
};

void CThread_Manager::Unlink_Thread_Node(TThread_List_Node* node) {
    if (!node) {
        return;
    }

    if (node == mThread_List_Head) {
        mThread_List_Head = node->next;
    }

    if (node->prev) {
        node->prev->next = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    }

    node->prev = nullptr;
    node->next = nullptr;
}

void CThread_Manager::Link_Thread_Node(TThread_List_Node* node) {
    if (mThread_List_Head) {
        node->next = mThread_List_Head;
        mThread_List_Head->prev = node;
    } else {
        node->next = nullptr;
        node->prev = nullptr;
    }
    mThread_List_Head = node;
};
