#include "drivers/timer.h"
#include "memory/mmu.h"
#include "memory/pages.h"
#include "memory/pt_alloc.h"
#include "process/process.h"
#include <process/process_manager.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <process/thread_manager.h>
#include <stdstring.h>

CScheduler sScheduler;

extern "C" {
void context_switch(TCPU_Context* ctx_to, TCPU_Context* ctx_from);
void context_switch_first(TCPU_Context* ctx_to, TCPU_Context* ctx_from);
};

CScheduler::CScheduler() : mCurrent_Thread(nullptr) {
    // mSchedule_Fnc = &CScheduler::Schedule_RR;
    mSchedule_Fnc = &CScheduler::Schedule_EDF;
}

TThread_List_Node* CScheduler::Schedule_RR() {
    // je nejaky proces naplanovany? pokud je ve stavu running, budeme snizovat citac, pokud ne, musime okamzite preplanovat
    if (mCurrent_Thread && mCurrent_Thread->state == NThread_State::Running) {

        if (mCurrent_Thread->sched_counter == 0) {
            for (;;)
                ; // hang
        }

        // snizime citac planovace
        mCurrent_Thread->sched_counter--;
        // pokud je citac vetsi nez 0, zatim nebudeme preplanovavat (a zaroven je proces stale ve stavu Running - nezablokoval se nad necim)
        if (mCurrent_Thread->sched_counter > 0) {
            return nullptr;
        }
    }

    // najdeme dalsi proces na planovani

    // vybereme dalsi proces v rade
    TThread_List_Node* node = mCurrent_Thread_Node && mCurrent_Thread_Node->next ? mCurrent_Thread_Node->next : sThread_Manager.Get_Thread_List_Head();

    // proces k naplanovani musi bud byt ve stavu runnable (jiz nekdy bezel a muze bezet znovu) nebo running (pak jde o stavajici proces)
    // a nebo new (novy proces, ktery jeste nebyl planovany)
    while (node->thread->state != NThread_State::Runnable && node->thread->state != NThread_State::Running && node->thread->state != NThread_State::New) {
        if (!node->next) {
            node = sThread_Manager.Get_Thread_List_Head();
        } else {
            node = node->next;
        }
    }

    // pokud chceme preplanovavat soucasny proces...
    if (node == mCurrent_Thread_Node) {
        // pokud by se mel preplanovat kvuli vyprseni casovych kvant...
        if (mCurrent_Thread_Node->thread->sched_counter == 0) {
            // nastavime mu zase zpatky jeho pridel casovych kvant a vracime se
            mCurrent_Thread_Node->thread->sched_counter = mCurrent_Thread_Node->thread->process->sched_static_priority;
        }
    }

    return node;
}

TThread_List_Node* CScheduler::Schedule_EDF() {
    TThread_List_Node* next = nullptr;

    // EDF = najdeme takovy proces, ktery je ve stavu Running nebo Runnable a ma deadline nejblizsi aktualni casove znacce

    if (mCurrent_Thread_Node == nullptr) // inicializace kdyby se nam nahodou ztratil current thread node (ukoncil se a uklidil po sobe)
        mCurrent_Thread_Node = sThread_Manager.Get_Thread_List_Head();

    TThread_List_Node* itr = mCurrent_Thread_Node;
    while (itr) {
        // potrebujeme, aby proces byl planovatelny (novy, bezici nebo runnable)
        if (itr->thread->state != NThread_State::New && itr->thread->state != NThread_State::Runnable && itr->thread->state != NThread_State::Running) {
            itr = itr->next;
            continue;
        }

        // "v nejhorsim" naplanujeme i takovy proces, ktery nema deadline
        if (!next) {
            next = itr;
            // pokud tento proces ma deadline...
        } else if (itr->thread->deadline != Indefinite) {
            // vybereme ho jen pokud je deadline drivejsi, nez aktualne vybraneho procesu
            // TODO: preteceni uint32_t
            if (next->thread->deadline > itr->thread->deadline) {
                next = itr;
            }
        }

        itr = itr->next;
    }

    // pokud zadny planovatelny proces nema nastavenou deadline, degradujeme na round-robin planovani
    if (next && next->thread->deadline == Indefinite) {
        return Schedule_RR();
    }

    return next;
}

TThread_Struct* CScheduler::Get_Current_Thread() const { return mCurrent_Thread; }
TThread_List_Node* CScheduler::Get_Current_Thread_Node() const { return mCurrent_Thread_Node; };
TProcess_Struct* CScheduler::Get_Current_Process() const { return mCurrent_Thread ? mCurrent_Thread->process : nullptr; };
uint32_t CScheduler::Get_Current_Pid() const { return mCurrent_Thread ? mCurrent_Thread->process->pid : Invalid_PID; };

void CScheduler::Block_Current_Thread() {
    TThread_Struct* cur = Get_Current_Thread();

    cur->state = NThread_State::Blocked;

    // xTODO: doopravdy to takto chceme?
    // if (cur->notified_deadline != Deadline_Unchanged)
    //    cur->deadline = Indefinite;

    Schedule();
}

void CScheduler::Schedule() {
    // inicializace neni potreba, kdyztak ji dela planovac
    /*if (!mCurrent_Thread) {
        mCurrent_Thread_Node = sThread_Manager.Get_Thread_List_Head();
        mCurrent_Thread = mCurrent_Thread_Node->thread;
    }*/

    // projdeme vsechny uspane (sleep) procesy, zda je na case je vzbudit
    {
        TThread_List_Node* node = sThread_Manager.Get_Thread_List_Head();
        while (node) {
            if (node->thread->state == NThread_State::Interruptable_Sleep) {
                if (node->thread->sleep_timer != Indefinite && node->thread->sleep_timer <= sTimer.Get_Tick_Count()) {
                    sThread_Manager.Notify_Thread(node->thread);
                }
            }

            node = node->next;
        }
    }

    // invokujeme skutecny planovac
    TThread_List_Node* next = (this->*mSchedule_Fnc)();
    if (!next) {
        return;
    }

    // tady bychom jeste meli osetrit nejakou hranicni situaci, kdy by nebylo co naplanovat - to se sice nesmi stat a byla by to chyba programatora kernelu,
    // ale kdyby k tomu doslo, obtizne by se to diagnostikovalo

    // planovac usoudil, ze ma byt naplanovany proces, ktery zrovna ma CPU pro sebe - nemusime nic menit
    if (next == mCurrent_Thread_Node) {
        return;
    }

    Switch_To(next);

    // TODO:
    if (mCleanup_Contents.do_cleanup) {
        Deferred_Cleanup();
    }
}

void CScheduler::Switch_To(TThread_List_Node* node) {
    // pokud je stavajici proces ve stavu Running (muze teoreticky byt jeste Blocked), vratime ho do stavu Runnable
    // Blocked prehazovat nebudeme ze zjevnych duvodu
    if (mCurrent_Thread->state == NThread_State::Running) {
        mCurrent_Thread->state = NThread_State::Runnable;
    }

    // projistotu vynulujeme prideleny pocet casovych kvant
    mCurrent_Thread->sched_counter = 0;

    TCPU_Context* old = &mCurrent_Thread->cpu_context;

    // prehodime na novy proces, pridelime casova kvanta a nastavime proces do stavu Running
    mCurrent_Thread = node->thread;
    bool is_first_time = (mCurrent_Thread->state == NThread_State::New);
    mCurrent_Thread_Node = node;
    // TODO: nastavit current process u process manageru
    // + zamyslet se kde by to melo byt, jestli tady ve scheduleru nebo to nechat v procesu
    mCurrent_Thread->sched_counter = mCurrent_Thread->process->sched_static_priority;
    mCurrent_Thread->state = NThread_State::Running;

    // pokud je to poprve, co je proces planovany, musime to vzit jeste pres malou odbocku ("bootstrap")
    if (is_first_time) {
        context_switch_first(&node->thread->cpu_context, old);
    } else {
        context_switch(&node->thread->cpu_context, old);
    }
}

void CScheduler::Sign_Up_For_Deferred_Cleanup(bool free_page_table, bool unlink_thread_node) {
    uint32_t* pt = mCurrent_Thread->process->page_table;

    // NOTE: pocitame stack z TID a ne aktualniho sp, kdybychom v budoucnu chteli v kernelu resit stack overflow/stack corruption
    // uint32_t stack_page_virt = (cur_thread->cpu_context.sp / PT_Region_Size) * PT_Region_Size;
    uint32_t stack_page_virt = (Stack_Area_Virtual_Base + Stack_Guard_Size + (Stack_Size + Stack_Guard_Size) * mCurrent_Thread->tid);
    uint32_t stack_page_phys = (pt[stack_page_virt / PT_Region_Size] & 0xFFF00000);

    mCleanup_Contents = TDeferred_Thread_Cleanup{
        .do_cleanup = true,
        .unlink_thread_node = unlink_thread_node,
        .thread_node = mCurrent_Thread_Node,
        .stack_page_virt = stack_page_virt,
        .stack_page_phys = stack_page_phys,
        .do_free_pt = free_page_table,
        .page_table = pt,
    };
}

void CScheduler::Deferred_Cleanup() {
    sPage_Manager.Free_Page(mCleanup_Contents.stack_page_phys);

    if (mCleanup_Contents.do_free_pt) {
        // free whole pt
        sPT_Alloc.Free(mCleanup_Contents.page_table);

    } else {
        // unmap stack from pt
        unmap_memory(mCleanup_Contents.page_table, mCleanup_Contents.stack_page_virt);
    }

    if (mCleanup_Contents.unlink_thread_node){
        sThread_Manager.Unlink_Thread_Node(mCleanup_Contents.thread_node);
    }

    mCleanup_Contents.do_cleanup = false;
}

bool CScheduler::Get_Scheduler_Info(NGet_Sched_Info_Type type, void* target) {
    if (!target) {
        return false;
    }

    switch (type) {
        case NGet_Sched_Info_Type::Active_Process_Count: {
            TThread_List_Node* node = sThread_Manager.Get_Thread_List_Head();
            uint32_t procCnt = 0;
            while (node) {
                if (node->thread->state == NThread_State::Running || node->thread->state == NThread_State::Runnable ||
                    node->thread->state == NThread_State::New) {
                    procCnt++;
                }

                node = node->next;
            }

            *reinterpret_cast<uint32_t*>(target) = procCnt;
            break;
        }
        case NGet_Sched_Info_Type::Tick_Count: {
            *reinterpret_cast<uint32_t*>(target) = sTimer.Get_Tick_Count();
            break;
        }
        case NGet_Sched_Info_Type::Process_Summary: {
            // TODO: predelat aby to odpovidalo bud process summary, nebo rename na thread summary
            // idealne udelat oboji at tu je process i thread summary, a pripojit do procfs summary
            TThread_List_Node* node = sThread_Manager.Get_Thread_List_Head();
            uint32_t procCnt = 0;

            CProcess_Summary_Info* sum = reinterpret_cast<CProcess_Summary_Info*>(target);
            bzero(sum, sizeof(CProcess_Summary_Info));

            while (node) {
                switch (node->thread->state) {
                    case NThread_State::Running:
                    case NThread_State::Runnable:
                        sum->running++;
                        break;
                    case NThread_State::Blocked:
                    case NThread_State::Interruptable_Sleep:
                        sum->blocked++;
                        break;
                    case NThread_State::Zombie:
                        sum->zombie++;
                        break;
                    default:
                        break;
                }

                sum->total++;
                node = node->next;
            }
            break;
        }
        default:
            return false;
    }
    return true;
}
