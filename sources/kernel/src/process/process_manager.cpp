// #include <cstdint>
#include <process/elf_loader.h>
#include <process/process.h>
#include <process/process_manager.h>
#include <process/resource_manager.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <process/thread_manager.h>

#include <memory/kernel_heap.h>
#include <memory/memmap.h>
#include <memory/mmu.h>
#include <memory/pages.h>
#include <memory/pt_alloc.h>

#include <fs/filesystem.h>

#include <drivers/timer.h>
#include <drivers/uart.h>

CProcess_Manager sProcessMgr;

CProcess_Manager::CProcess_Manager() : mLast_PID(0), mProcess_List_Head(nullptr) {}

TProcess_List_Node* CProcess_Manager::Get_Process_Node_By_PID(uint32_t pid) const {
    TProcess_List_Node* node = mProcess_List_Head;
    while (node != nullptr) {
        if (node->process->pid == pid) {
            return node;
        }

        node = node->next;
    }

    return nullptr;
}

TProcess_Struct* CProcess_Manager::Get_Process_By_PID(uint32_t pid) const {
    TProcess_List_Node* node = Get_Process_Node_By_PID(pid);
    if (!node) {
        return nullptr;
    }
    return node->process;
}

void CProcess_Manager::Unlink_Process_Node(TProcess_List_Node* node) {
    if (!node) {
        return;
    }

    if (node == mProcess_List_Head) {
        mProcess_List_Head = node->next;
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

uint32_t CProcess_Manager::Notify_Process(uint32_t pid, IFile* notifier_file) {
    TProcess_Struct* process = Get_Process_By_PID(pid);
    if (!process) {
        return false;
    }

    // najdeme index otevreneho souboru
    uint32_t handle_index = 0;
    for (handle_index = 0; handle_index < Max_Process_Opened_Files; ++handle_index) {
        if (process->opened_files[handle_index] == notifier_file) {
            break;
        }
    }

    if (handle_index == Max_Process_Opened_Files) {
        // tohle by nemelo nastat pokud kernel funguje spravne
        return false;
    }

    // postupne vzbudime vsechny vlakna ktere cekaji nad danym souborem
    uint32_t notified_count = 0;
    for (TThread_Struct* thread : process->threads) {
        if (thread->multi_wait_fd_bitmap & 1 << handle_index) {
            if (thread->is_multi_file_wait) {
                thread->multi_wait_ready_fd = handle_index;
            }
            else {
                thread->multi_wait_fd_bitmap |= 1 << handle_index; // guard proti tomu aby nam nekdo na dany file handle nenamapaoval novy soubor, kdyz budeme prepinat kontext na toto vlakno tak to vynulujeme
            }
            notified_count += static_cast<uint32_t>(sThread_Manager.Notify_Thread(thread));
        }
    }

    return notified_count;
}

uint32_t CProcess_Manager::Create_Process(unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system, uint32_t sched_static_priority) {
    if (sched_static_priority == 0) { // pokud nekdo vyrobil proces se statickou prioritou 1 tak nema cenu ho vubec vytvaret, planovac by ho nikdy nenaplanoval
        return Invalid_PID;
    }
    
    TProcess_List_Node* procnode = sKernelMem.Alloc<TProcess_List_Node>();

    // zarazeni do spojoveho seznamu procesu
    procnode->next = mProcess_List_Head;
    procnode->prev = nullptr;
    mProcess_List_Head->prev = procnode;
    mProcess_List_Head = procnode;

    // alokace pameti pro process data
    procnode->process = sKernelMem.Alloc<TProcess_Struct>();
    auto* process = procnode->process;
    // nastaveni process data
    process->pid = ++mLast_PID;
    process->sched_static_priority = sched_static_priority; 
    process->page_count = 0;
    process->thread_count = 0;

    // nullpointery na vsechna vlakna - zakladat se da jenom tam kde je nullpointer (hlida thread manager)
    for (uint32_t i = 0; i < Max_Process_Threads; i++) {
        process->threads[i] = nullptr;
    }

    // zalozime prvni vlakno procesu
    if (sThread_Manager.Create_First_Thread(process, elf_file_data, elf_file_length, is_system) == Invalid_TID) {
        // pokud se nepodarilo zalozeni, uvolnime alokovane zdroje a vratime chybu
        sKernelMem.Free(procnode->process);
        sKernelMem.Free(procnode);
        return Invalid_PID;
    }


    // nullpointery na otevrene soubory
    for (uint32_t i = 0; i < Max_Process_Opened_Files; i++) {
        process->opened_files[i] = nullptr;
    }

    return process->pid;
}



void CProcess_Manager::Terminate_Process(TProcess_Struct* proc, uint32_t exit_code) {
    // zavreme vsechny soubory procesu - to muzeme
    Close_All_Files(proc);
   
    // zabranime v behu vsech vlaken tohoto procesu
    for (TThread_Struct* thread : proc->threads) {
        thread->state = NThread_State::Zombie;
    }
    proc->exit_code = exit_code;
    // uklid a vyber navratove hodnoty je zalezitost toho, kdo proces bude sklizet.
    // bud jiny proces, nebo na velkem systemu by to byl init. Vzhledem k tomu, ze neumime dynamicky zavadet procesy neni treba resit dalsi implementaci.
}

uint32_t CProcess_Manager::Reap_Process(uint32_t pid) { 
    // najdeme proces, ktery sklizime

    auto* procnode = Get_Process_Node_By_PID(pid);
    if (!procnode) return Invalid_PID; // pokud dany proces neexistuje tak vratime neco napadne spatneho
    auto* proc = procnode->process; // tahle dereference musi projit, pokud ne tak mame vetsi problem nez ze dereferujeme nullptr
    for (TThread_Struct* thread : proc->threads) { 
        if (thread)
            if (thread->state != NThread_State::Zombie) { //pokud nejake vlakno neni zobmie (chteli bychom sklidit proces ktery jeste neskoncil)
                return Invalid_PID; //tak vratime neco napadne spatneho
            }
    }
    for (TThread_Struct* thread : proc->threads) { //uvolnime vsechna vlakna - zde zcela rizene obchazim komentar, protoze ten se tyka ukoncovani vlakna z POV vlakna
        if (thread) {
            sThread_Manager.Free_Thread_Resources(proc, thread);
        }
    }

    // a na zaver vyzvedneme exit code a uklidime proces
    uint32_t exit_code = proc->exit_code;
    Unlink_Process_Node(procnode);
    sKernelMem.Free(procnode);
    sPT_Alloc.Free(proc->page_table);
    sKernelMem.Free(proc);
    return 0; 
}


uint32_t CProcess_Manager::Map_File_To_Current(IFile* file) {
    // xTODO: zamek

    TProcess_Struct* current = sScheduler.Get_Current_Process();
    if (!current) {
        return Invalid_Handle;
    }

    // najdeme volny slot, pokud je
    bool slot_in_use_by_waiting_thread = false;               // jestli nad vybranym prazdnym file handlem nekdo ceka
    for (uint32_t i = 0; i < Max_Process_Opened_Files; i++) { // kontrolujeme vsechny volne sloty
        if (current->opened_files[i] == nullptr) {            // pokud je volny slot
            for (TThread_Struct* thread : current->threads) { // tak musime u vsech vlaken zkontrolovat jestli nad nim necekaji (koncept dead-wait)
                if (thread->multi_wait_fd_bitmap & 1 << i) {
                    slot_in_use_by_waiting_thread = true;
                    break;
                }
            }
            if (!slot_in_use_by_waiting_thread) { // pokud nad timhle slotem nikdo neceka, tak ho muzeme pouzit
                current->opened_files[i] = file;
                return i;
            }
            slot_in_use_by_waiting_thread = false; // pokud nad nim nekdo ceka tak vyresetujeme promennou a jdeme na dalsi file handle
        }
    }

    // nenasli jsme - vracime neplatny handle (vnejsi kod nejspis bude chtit soubor zase zavrit)
    return Invalid_Handle;
}

bool CProcess_Manager::Unmap_File_Current(uint32_t handle) {
    TProcess_Struct* current = sScheduler.Get_Current_Process();
    if (!current || handle >= Max_Process_Opened_Files || !current->opened_files[handle]) {
        return false;
    }

    // posleme vsem vlaknum co na odmapovavany proces cekaji notify, aby se vzbudili (read jim vrati neplatnou hodnotu)
    Notify_Process(current->pid, current->opened_files[handle]); // navratovou hodnotu nepotrebujeme
    current->opened_files[handle] = nullptr;                     // a pak muzeme soubor odmapovat

    return true;
}

void CProcess_Manager::Close_All_Files(TProcess_Struct* process) {
    if (!process) {
        return;
    }
    for (uint32_t i = 0; i < Max_Process_Opened_Files; i++) {
        if (process->opened_files[i]) {
            process->opened_files[i]->Close();
            Notify_Process(process->pid, process->opened_files[i]); // notifikujeme vsechna vlakna v procesu ze dany soubor byl zavren
            process->opened_files[i] = nullptr;
        }
    }
}

void CProcess_Manager::Handle_Filesystem_SWI(NSWI_Filesystem_Service svc_idx, uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target) {

    TProcess_Struct* cur_process = sScheduler.Get_Current_Process();
    // xTODO: signalizace chyby
    if (!cur_process) {
        return;
    }

    switch (svc_idx) {
        case NSWI_Filesystem_Service::Open: {
            target.r0 = Invalid_Handle;

            IFile* f = sFilesystem.Open(reinterpret_cast<const char*>(r0), static_cast<NFile_Open_Mode>(r1));
            if (!f) {
                return;
            }

            target.r0 = Map_File_To_Current(f);

            // nepodarilo se namapovat, napr. proto, ze jsme dosahli limitu otevrenych souboru
            if (target.r0 == Invalid_Handle) {
                f->Close();
                delete f;
            }
            break;
        }
        case NSWI_Filesystem_Service::Read: {
            target.r0 = 0;

            if (r0 > Max_Process_Opened_Files || !cur_process->opened_files[r0]) {
                return;
            }

            target.r0 = cur_process->opened_files[r0]->Read(reinterpret_cast<char*>(r1), r2);
            break;
        }
        case NSWI_Filesystem_Service::Write: {
            target.r0 = 0;

            if (r0 > Max_Process_Opened_Files || !cur_process->opened_files[r0]) {
                return;
            }

            target.r0 = cur_process->opened_files[r0]->Write(reinterpret_cast<const char*>(r1), r2);
            break;
        }
        case NSWI_Filesystem_Service::Close: {
            if (r0 > Max_Process_Opened_Files || !cur_process->opened_files[r0]) {
                return;
            }

            target.r0 = cur_process->opened_files[r0]->Close();
            Unmap_File_Current(r0);

            break;
        }
        case NSWI_Filesystem_Service::IOCtl: {
            if (r0 > Max_Process_Opened_Files || !cur_process->opened_files[r0]) {
                return;
            }

            target.r0 = cur_process->opened_files[r0]->IOCtl(static_cast<NIOCtl_Operation>(r1), reinterpret_cast<void*>(r2));
            break;
        }
        case NSWI_Filesystem_Service::Notify: {
            if (r0 > Max_Process_Opened_Files || !cur_process->opened_files[r0]) {
                return;
            }

            target.r0 = cur_process->opened_files[r0]->Notify(r1);
            break;
        }
        case NSWI_Filesystem_Service::Wait: {
            auto* cur_thread = sScheduler.Get_Current_Thread(); // v pripade wait musime zjistit ktere vlakno vola

            if (r0 > Max_Process_Opened_Files || !cur_process->opened_files[r0]) {
                return;
            }

            cur_thread->notified_deadline = r2;

            // nastavme deadline, kdyby Wait nahodou neblokoval
            if (r2 != Deadline_Unchanged) {
                cur_thread->deadline = r2;
            }

            target.r0 = cur_process->opened_files[r0]->Wait(r1); // Wait volame z POV vlakna

            // v tento moment uz lze "budouci" deadline vymazat, uz je nastavena ve skutecne deadline bud shora nebo z notify kodu
            cur_thread->notified_deadline = Deadline_Unchanged;
            break;
        }
        case NSWI_Filesystem_Service::WaitAll: {
            Handle_WaitAll_SWI(r0, r1, r2, target);
            break;
        }
    }
}

// dTODO: TThread_struct misto TTask_Struct, jinak by mohlo fungovat stejne
// jinak tohle mooozna by mohlo byt v thread manageru, ale jako nevim no, tyka se to procesu i vlakna
void CProcess_Manager::Handle_WaitAll_SWI(uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target) {
    target.r0 = Invalid_Handle;

    uint32_t* handles = reinterpret_cast<uint32_t*>(r0);
    uint32_t handle_count = r1;

    if (handle_count == 0 || handles == nullptr) {
        return;
    }

    TProcess_Struct* cur_process = sScheduler.Get_Current_Process();

    for (uint32_t i = 0; i < handle_count; i++) {
        if (handles[i] >= Max_Process_Opened_Files || !cur_process->opened_files[handles[i]]) {
            return;
        }
    }

    auto* cur_thread = sScheduler.Get_Current_Thread();
    // nastaveni deadline, kdyby nahodou nejaky soubor uz byl ready - nastavujeme pro vlakno, ktere zavolalo tento syscall
    cur_thread->notified_deadline = r2;
    if (r2 != Deadline_Unchanged) {
        cur_thread->deadline = r2;
    }

    // nejprve zkusime, zda neni nektery ze souboru jiz pripraven - pokud ano, vratime hned jeho handle a nebudeme blokovat vlakno
    for (uint32_t i = 0; i < handle_count; i++) {
        if (cur_process->opened_files[handles[i]]->Try_Wait_All_Reserve(1)) {
            target.r0 = handles[i];
            cur_thread->multi_wait_ready_fd = handles[i];
            return;
        }
    }

    // Pokud dosud nebyl zadny soubor pripraven, aktualni process se ke vsem souborum zaregistruje do seznamu cekajicich procesu, a proces se zablokuje
    cur_thread->is_multi_file_wait = true;
    cur_thread->multi_wait_ready_fd = Invalid_Handle;
    cur_thread->multi_wait_fd_bitmap = 0;

    for (uint32_t i = 0; i < handle_count; i++) {
        cur_thread->multi_wait_fd_bitmap |= (1 << handles[i]);
        cur_process->opened_files[handles[i]]->Wait_Enqueue_Current();
    }

    bool file_ready = false;
    while (!file_ready) {
        sScheduler.Block_Current_Thread();

        // pokusime se po probuzeni ziskat resource od probouzejiciho souboru, a pokud se to povede, smycka skonci
        if (cur_thread->multi_wait_ready_fd != Invalid_Handle && cur_process->opened_files[cur_thread->multi_wait_ready_fd]->Wait_All_Acquire(1)) {
            // do navratove hodnoty dame handle soubor ktery proces uspesne probudil
            target.r0 = cur_thread->multi_wait_ready_fd;
            file_ready = true;

            // Dequeue ze vsech souboru, aby vlakno nebyl notifikovano znovu
            for (uint32_t i = 0; i < handle_count; i++) {
                cur_process->opened_files[handles[i]]->Wait_Dequeue_Thread(CThread_Manager::GetPIDTID(cur_thread));
            }
        } else {
            // resource jsme neziskali - mozna doslo k falesnemu probuzeni, nebo nas nekdo jiny probudil
            cur_thread->multi_wait_ready_fd = Invalid_Handle;
        }
        //  pokud resource nebyl po probuzeni ziskan - zablokuj se znovu
    }

    // cleanup
    cur_thread->is_multi_file_wait = false;
    cur_thread->multi_wait_fd_bitmap = 0;

    // v tento moment uz lze "budouci" deadline vymazat, uz je nastavena ve skutecne deadline bud shora nebo z notify kodu
    cur_thread->notified_deadline = Deadline_Unchanged;
}

uint32_t CProcess_Manager::Get_Page_Count() {
    TProcess_List_Node* node = mProcess_List_Head;
    uint32_t c = 0;
    while (node != nullptr) {
        c += node->process->page_count;
        node = node->next;
    }

    return c;
}

uint32_t CProcess_Manager::Get_File_Count() {
    TProcess_List_Node* node = mProcess_List_Head;
    uint32_t c = 0;
    while (node != nullptr) {
        for (int i = 0; i < Max_Process_Opened_Files; i++) {
            if (node->process->opened_files[i] != nullptr)
                c++;
        }
        node = node->next;
    }

    return c;
}
