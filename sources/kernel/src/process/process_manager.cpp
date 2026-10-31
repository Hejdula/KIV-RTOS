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
//CThread_Manager sThreadMgr; kdo je za tohle zodpovedny tomu hlava upadne

CProcess_Manager::CProcess_Manager() : mLast_PID(0), mProcess_List_Head(nullptr), mCurrent_Process_Node(nullptr) {}

TProcess_Struct* CProcess_Manager::Get_Current_Process() const { return mCurrent_Process_Node ? mCurrent_Process_Node->process : nullptr; }

CProcess_List_Node* CProcess_Manager::Get_Process_Node_By_PID(uint32_t pid) const {
    CProcess_List_Node* node = mProcess_List_Head;
    while (node != nullptr) {
        if (node->process->pid == pid) {
            return node;
        }

        node = node->next;
    }

    return nullptr;
}

TProcess_Struct* CProcess_Manager::Get_Process_By_PID(uint32_t pid) const {
    CProcess_List_Node* node = Get_Process_Node_By_PID(pid);
    if (!node) {
        return nullptr;
    }
    return node->process;
}

bool CProcess_Manager::Notify_Process(uint32_t pid, IFile* notifier_file) {
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
    bool notified_some;
    for (TThread_Struct* thread : process->threads) {
        if (thread->multi_wait_fd_bitmap & 1 << handle_index) {
            if (thread->is_multi_file_wait) {
                thread->multi_wait_ready_fd = handle_index;
            }
            notified_some |= sThread_Manager.Notify_Thread(thread);
        }
    }

    return notified_some;
}

uint32_t CProcess_Manager::Create_Process(unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system) {
    CProcess_List_Node* procnode = sKernelMem.Alloc<CProcess_List_Node>();

    // zarazeni do spojoveho seznamu procesu
    procnode->next = mProcess_List_Head;
    procnode->prev = nullptr;
    mProcess_List_Head->prev = procnode;
    mProcess_List_Head = procnode;

    // TODO: je tohle k necemu?
    if (!mCurrent_Process_Node) {
        mCurrent_Process_Node = procnode;
    }

    // alokace pameti pro process data
    procnode->process = sKernelMem.Alloc<TProcess_Struct>();
    auto* process = procnode->process;
    // nastaveni process data
    process->pid = ++mLast_PID;
    process->sched_static_priority = 5; // xTODO: pro ted je to jen hardcoded hodnota, do budoucna urcite bude nutne dovolit specifikovat - by se mohlo proste
                                        // predavat jako argument funkce...
    process->page_count = 0;
    process->thread_count = 0;

    /* DTODO: - co by se tu melo dit - DONE
        - volam zakladani prvniho threadu - asi funkce "create first thread", ktera bude volat "create thread", ktera bude volana handlerem neceho jako
       "pthread_create()"
        - inkrementovat pocitadlo threadu, dat do seznamu threadu prvni thread
    */
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

    /* DTODO: tyhle veci se budou zakladat v threadu
    - dedime od processu
    - haldu resit nemusime, o tu si thread rika syscallem
    - vytvorit tabulku stranek pro process.
    - alokovat stranku pro kod a dat ho tam stejne jako ted
    - vytvoreni prvniho thread a jeho zasobniku + pc na entry point kodu



    process->sched_counter = process->sched_static_priority;
    process->state = Nprocess_State::New;
    process->deadline = Indefinite; // process si zatim nestanovil deadline, udela to az to bude aktualni v deadline syscallu
    process->notified_deadline = Indefinite;
    process->page_count = 0;


    // lr = co zacit vykonavat po bootstrapu, 0x8000 je misto, kam je relokovany v kazde nasi binarce symbol _start, tedy vstupni bod programu
    //process->cpu_context.lr = 0x8000;
    // nyni mame ale implementovany i ELF loader, takze po nacteni ELF souboru lze vycist, kde je vlastne vstupni bod programu

    // pc = "aktualni" kod k provadeni, tedy bootstrap procesu v jadre - lisi se fakticky jen tim, zda je do CPSR vlozen rezim uzivatelsky nebo systemovy,
    // ale teoreticky muzeme chtit pridat jeste dalsi veci specificke pro nejaky z rezimu
    process->cpu_context.pc = is_system ? reinterpret_cast<unsigned long>(&system_process_bootstrap) : reinterpret_cast<unsigned long>(&user_process_bootstrap);
    // sp = zasobnik, vzdy je relokovany na nejakou fixni pamet, pro nas je jednoduche volit ho o velikosti 1MB
    // pozn. zasobnik roste na druhou stranu, takze musime SP nastavit na konec stranky
    process->cpu_context.sp = 0x90000000 + mem::PageSize;

    // alokujeme stranku pro kod a pro zasobnik
    uint32_t code_page_phys = static_cast<unsigned long>(sPage_Manager.Alloc_Page()) - mem::MemoryVirtualBase;
    uint32_t stack_page_phys = static_cast<unsigned long>(sPage_Manager.Alloc_Page()) - mem::MemoryVirtualBase;
    process->page_count += 2;

    // alokujeme tabulku stranek procesu z poolu
    uint32_t* pt = sPT_Alloc.Alloc();

    // zkopirujeme zakladni kernelovou tabulku - to proto, aby zustalo mapovani 0xFxxxxxxx a 0xCxxxxxxx adres (kod a data) a mohli jsme vykonavat
    // napr. obsluhu systemoveho volani beze zmeny adresniho prostoru
    // pozn.: tohle kopirovani se da odbourat korektnim pouzitim TTBR1 - pak by TTBR1 prekladal vse napr. od 0x8xxxxxxx vyse a TTBR0 by zustal pro spodni cast
    adr. rozsahu
    //        pro jednoduchost ted nechme takto, dvojice bazovych registru pro tabulky stranek jsou spise specialita ARMu a pribuznych procesoru
    copy_kernel_page_table_to(pt);

    // namapujeme kodovou stranku na zacatek virtualni pameti
    map_memory(pt, code_page_phys, 0x00000000);
    // namapujeme zasobnik na 0x90000000
    map_memory(pt, stack_page_phys, 0x90000000);

    // nakopirujeme kod do kodove stranky, vysledkem nacitani je i PC vstupniho bodu procesu
    const uint32_t elf_pc = CELF_Loader::Load_ELF32_Image(elf_file_data, reinterpret_cast<unsigned char*>(code_page_phys) + mem::MemoryVirtualBase,
    mem::PageSize); if (elf_pc == CELF_Loader::Invalid_Entry_Point) {
        // pokud se nepodarilo nacteni, uvolnime alokovane zdroje a vratime chybu
        sPT_Alloc.Free(pt);
        sPage_Manager.Free_Page(code_page_phys + mem::MemoryVirtualBase);
        sPage_Manager.Free_Page(stack_page_phys + mem::MemoryVirtualBase);
        sKernelMem.Free(procnode->process);
        sKernelMem.Free(procnode);
        return Invalid_PID;
    }

    // nastavime PC procesu na vstupni bod programu, ktery jsme nacetli z ELF souboru
    process->cpu_context.lr = elf_pc;

    // nastavime ulozene TTBR0 procesu - tady musi byt fyzicka adresa, proto odecitame bazi (vsechny fyzicke adresy jsou v kernelovem modu mapovany na
    "0xC0000000 + offset") process->cpu_context.ttbr0 = (reinterpret_cast<unsigned long>(pt) - mem::MemoryVirtualBase) | TTBR_Flags::Inner_Cacheable |
    TTBR_Flags::Shared;
    */

    // nullpointery na otevrene soubory
    for (uint32_t i = 0; i < Max_Process_Opened_Files; i++) {
        process->opened_files[i] = nullptr;
    }

    return process->pid;
}

uint32_t CProcess_Manager::Map_File_To_Current(IFile* file) {
    // TODO: zamek

    TProcess_Struct* current = Get_Current_Process();
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
    TProcess_Struct* current = Get_Current_Process();
    if (!current || handle >= Max_Process_Opened_Files) {
        return false;
    }

    if (!current->opened_files[handle]) {
        return false;
    }
    // posleme vsem vlaknum co na odmapovavany proces cekaji notify, aby se vzbudili (read jim vrati neplatnou hodnotu)
    Notify_Process(current->pid, current->opened_files[handle]); // navratovou hodnotu nepotrebujeme
    current->opened_files[handle] = nullptr;                     // a pak muzeme soubor odmapovat

    // reseni implementovano
    // xTODO: bude treba resit kdyz ruzne threads na soubor cekaji,
    // ted se tohle stalo jen kdyz proces bezel a zavolal close,
    // handle se invalidovalo a pak uz nemohl nad nim cekat, ale ted muze close
    // zavolat jakekoli vlakno a tim padem ostatni kdo cekaji se musi vzbudit.
    // Co kdyz se mezitim nez se thread vzbudi na handle namapuje jiny soubor a tedy
    // jinemu thread se zmeni soubor na ktery ceka aniz by to vedel?
    // Nebo spise kazde vlakno ma svou bitmap s otevrenymi files a soubor ho
    // zavre jen kdyz uz zadne z vlaken nema soubor otevreny a pak by se tohle muselo
    // o level vyse v Handle_xxx_SWI

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

// dTODO: kam patri tohle
// Zustane to tady, mame pristup do scheduleru ze ktereho si kdyztak vyzvedneme soucasne vlakno, kdyby bylo potreba
void CProcess_Manager::Handle_Filesystem_SWI(NSWI_Filesystem_Service svc_idx, uint32_t r0, uint32_t r1, uint32_t r2, TSWI_Result& target) {

    TProcess_Struct* cur_process = Get_Current_Process();
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

    TProcess_Struct* cur_process = Get_Current_Process();

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

            // Dequeue ze vsech souboru, aby proces nebyl notifikovan znovu
            for (uint32_t i = 0; i < handle_count; i++) {
                cur_process->opened_files[handles[i]]->Wait_Dequeue_Process(cur_process->pid);
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
    CProcess_List_Node* node = mProcess_List_Head;
    uint32_t c = 0;
    while (node != nullptr) {
        c += node->process->page_count;
        node = node->next;
    }

    return c;
}

uint32_t CProcess_Manager::Get_File_Count() {
    CProcess_List_Node* node = mProcess_List_Head;
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
