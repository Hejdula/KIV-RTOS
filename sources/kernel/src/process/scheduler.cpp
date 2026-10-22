#include <process/process_manager.h>
#include <process/scheduler.h>



CScheduler::CScheduler() : mCurrent_Thread(nullptr) {
    //mSchedule_Fnc = &CScheduler::Schedule_RR;
    mSchedule_Fnc = &CScheduler::Schedule_EDF;
}


TThread_Struct* CScheduler::Schedule_RR() {
    // je nejaky proces naplanovany? pokud je ve stavu running, budeme snizovat citac, pokud ne, musime okamzite preplanovat
    if (mCurrent_Task_Node && mCurrent_Task_Node->task->state == NTask_State::Running) {
        // snizime citac planovace
        mCurrent_Task_Node->task->sched_counter--;
        // pokud je citac vetsi nez 0, zatim nebudeme preplanovavat (a zaroven je proces stale ve stavu Running - nezablokoval se nad necim)
        if (mCurrent_Task_Node->task->sched_counter > 0) {
            return nullptr;
        }
    }

    // najdeme dalsi proces na planovani

    // vybereme dalsi proces v rade
    CProcess_List_Node* next = mCurrent_Task_Node ? mCurrent_Task_Node->next : mProcess_List_Head;
    if (!next) {
        next = mProcess_List_Head;
    }

    // proces k naplanovani musi bud byt ve stavu runnable (jiz nekdy bezel a muze bezet znovu) nebo running (pak jde o stavajici proces)
    // a nebo new (novy proces, ktery jeste nebyl planovany)
    while (next->task->state != NTask_State::Runnable && next->task->state != NTask_State::Running && next->task->state != NTask_State::New) {
        if (!next) {
            next = mCurrent_Task_Node;
            break;
        } else {
            next = next->next;
        }
    }

    // pokud nechceme preplanovavat soucasny proces...
    if (next == mCurrent_Task_Node) {
        // pokud by se mel preplanovat kvuli vyprseni casovych kvant...
        if (mCurrent_Task_Node->task->sched_counter == 0) {
            // nastavime mu zase zpatky jeho pridel casovych kvant a vracime se
            mCurrent_Task_Node->task->sched_counter = mCurrent_Task_Node->task->sched_static_priority;
        }
    }

    return next;
}

TThread_Struct* CScheduler::Schedule_EDF() {
    CProcess_List_Node* next = nullptr;

    // EDF = najdeme takovy proces, ktery je ve stavu Running nebo Runnable a ma deadline nejblizsi aktualni casove znacce

    CProcess_List_Node* itr = mProcess_List_Head;
    while (itr) {
        // potrebujeme, aby proces byl planovatelny (novy, bezici nebo runnable)
        if (itr->task->state != NTask_State::New && itr->task->state != NTask_State::Runnable && itr->task->state != NTask_State::Running) {
            itr = itr->next;
            continue;
        }

        // "v nejhorsim" naplanujeme i takovy proces, ktery nema deadline
        if (!next) {
            next = itr;
        // pokud tento proces ma deadline...
        } else if (itr->task->deadline != Indefinite) {
            // vybereme ho jen pokud je deadline drivejsi, nez aktualne vybraneho procesu
            // TODO: preteceni uint32_t
            if (next->task->deadline > itr->task->deadline) {
                next = itr;
            }
        }

        itr = itr->next;
    }

    // pokud zadny planovatelny proces nema nastavenou deadline, degradujeme na round-robin planovani
    if (next && next->task->deadline == Indefinite) {
        return Schedule_RR();
    }

    return next;
}

TThread_Struct* CScheduler::Get_Current_Thread() const {
    return mCurrent_Thread;
}

void CScheduler::Block_Current_Thread() {
    TThread_Struct* cur = Get_Current_Thread();

    cur->state = NThread_State::Blocked;

    // xTODO: doopravdy to takto chceme?
    //if (cur->notified_deadline != Deadline_Unchanged)
    //    cur->deadline = Indefinite;

    Schedule();
}

// TODO: presuneme do CScheduler, bude planovat jednotlive threads misto task/process,
// thread pak potrebuje mit svou deadline dle ktere se bude planovat
void CScheduler::Schedule() {

    // projdeme vsechny uspane (sleep) procesy, zda je na case je vzbudit
    {
        CProcess_List_Node* node = mProcess_List_Head;
        while (node) {
            if (node->task->state == NTask_State::Interruptable_Sleep) {
                if (node->task->sleep_timer != Indefinite && node->task->sleep_timer <= sTimer.Get_Tick_Count()) {
                    Notify_Thread(node->task); // TODO: je treba prochazet/planovat vlakna misto procesu
                }
            }

            node = node->next;
        }
    }

    // invokujeme skutecny planovac
    CProcess_List_Node* next = (this->*mSchedule_Fnc)();
    if (!next) {
        return;
    }

    // tady bychom jeste meli osetrit nejakou hranicni situaci, kdy by nebylo co naplanovat - to se sice nesmi stat a byla by to chyba programatora kernelu,
    // ale kdyby k tomu doslo, obtizne by se to diagnostikovalo

    // planovac usoudil, ze ma byt naplanovany proces, ktery zrovna ma CPU pro sebe - nemusime nic menit
    if (next == mCurrent_Task_Node) {
        return;
    }

    Switch_To(next);
}

void CScheduler::Switch_To(CProcess_List_Node* node) {
    // pokud je stavajici proces ve stavu Running (muze teoreticky byt jeste Blocked), vratime ho do stavu Runnable
    // Blocked prehazovat nebudeme ze zjevnych duvodu
    if (mCurrent_Task_Node->task->state == NTask_State::Running) {
        mCurrent_Task_Node->task->state = NTask_State::Runnable;
    }

    // projistotu vynulujeme prideleny pocet casovych kvant
    mCurrent_Task_Node->task->sched_counter = 0;

    TCPU_Context* old = &mCurrent_Task_Node->task->cpu_context;
    bool is_first_time = (node->task->state == NTask_State::New);

    // prehodime na novy proces, pridelime casova kvanta a nastavime proces do stavu Running
    mCurrent_Task_Node = node;
    mCurrent_Task_Node->task->sched_counter = mCurrent_Task_Node->task->sched_static_priority;
    mCurrent_Task_Node->task->state = NTask_State::Running;

    // pokud je to poprve, co je proces planovany, musime to vzit jeste pres malou odbocku ("bootstrap")
    if (is_first_time) {
        context_switch_first(&node->task->cpu_context, old);
    } else {
        context_switch(&node->task->cpu_context, old);
    }
}

bool CScheduler::Get_Scheduler_Info(NGet_Sched_Info_Type type, void* target) {
    if (!target) {
        return false;
    }

    switch (type) {
        case NGet_Sched_Info_Type::Active_Process_Count: {
            CProcess_List_Node* node = mProcess_List_Head;
            uint32_t procCnt = 0;
            while (node) {
                if (node->task->state == NTask_State::Running || node->task->state == NTask_State::Runnable) {
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
            CProcess_List_Node* node = mProcess_List_Head;
            uint32_t procCnt = 0;

            CProcess_Summary_Info *sum = reinterpret_cast<CProcess_Summary_Info*>(target);
            bzero(sum, sizeof(CProcess_Summary_Info));

            while (node) {
                switch (node->task->state) {
                    case NTask_State::Running:
                    case NTask_State::Runnable:
                        sum->running++;
                        break;
                    case NTask_State::Blocked:
                    case NTask_State::Interruptable_Sleep:
                        sum->blocked++;
                        break;
                    case NTask_State::Zombie:
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
