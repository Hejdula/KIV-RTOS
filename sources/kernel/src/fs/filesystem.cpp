#include <fs/filesystem.h>

#include <memory/kernel_heap.h>
#include <process/process_manager.h>
#include <process/scheduler.h>
#include <process/thread_manager.h>
#include <stdstring.h>

#include <drivers/uart.h>

CFilesystem sFilesystem;

CFilesystem::CFilesystem() {
    // inicializujeme zakladni strukturu (tedy vlastne obsah root adresare)

    mRoot.parent = nullptr;
    mRoot.next = nullptr;
    mRoot.children = &mRoot_Dev;
    mRoot.isDirectory = true;
    mRoot.driver_idx = NoFilesystemDriver;
    mRoot.name[0] = '\0';

    mRoot_Dev.parent = &mRoot;
    mRoot_Dev.children = nullptr;
    mRoot_Dev.next = &mRoot_Sys;
    mRoot_Dev.isDirectory = true;
    mRoot_Dev.driver_idx = NoFilesystemDriver;
    strncpy(mRoot_Dev.name, "DEV", 4);

    mRoot_Sys.parent = &mRoot;
    mRoot_Sys.children = nullptr;
    mRoot_Sys.next = &mRoot_Mnt;
    mRoot_Sys.isDirectory = true;
    mRoot_Sys.driver_idx = NoFilesystemDriver;
    strncpy(mRoot_Sys.name, "SYS", 4);

    mRoot_Mnt.parent = &mRoot;
    mRoot_Mnt.children = nullptr;
    mRoot_Mnt.next = &mRoot_Proc;
    mRoot_Mnt.isDirectory = true;
    mRoot_Mnt.driver_idx = NoFilesystemDriver;
    strncpy(mRoot_Mnt.name, "MNT", 4);

    mRoot_Proc.parent = &mRoot;
    mRoot_Proc.children = nullptr;
    mRoot_Proc.next = nullptr;
    mRoot_Proc.isDirectory = true;
    mRoot_Proc.driver_idx = NoFilesystemDriver;
    strncpy(mRoot_Proc.name, "PROC", 5);
}

CFilesystem::TFS_Tree_Node* CFilesystem::TFS_Tree_Node::Find_Child(const char* name) {
    TFS_Tree_Node* child = children;

    while (child != nullptr) {
        if (strncmp(child->name, name, MaxFilenameLength) == 0) {
            return child;
        }

        child = child->next;
    }

    return nullptr;
}

void CFilesystem::Initialize() {
    char tmpName[MaxFilenameLength];
    const char* mpPtr;

    int i, j;

    for (i = 0; i < gFS_Drivers_Count; i++) {
        const TFS_Driver* ptr = &gFS_Drivers[i];

        mpPtr = ptr->mountPoint;

        TFS_Tree_Node* node = &mRoot, *tmpNode = nullptr;

        while (mpPtr[0] != '\0') {
            for (j = 0; j < MaxPathLength && mpPtr[j] != '\0'; j++) {
                if (mpPtr[j] == ':' || mpPtr[j] == '/') {
                    break;
                }

                tmpName[j] = mpPtr[j];
            }

            tmpName[j] = '\0';
            
            mpPtr += j;

            // jedna se o konec retezce?
            if (mpPtr[0] != '\0') {
                mpPtr += 1;
            }

            tmpNode = node->Find_Child(tmpName);
            // uzel jsme nasli - pouzijeme ho pro dalsi prohledavani
            if (tmpNode) {
                node = tmpNode;
            // uzel jsme nenasli - vytvorime ho a pouzijeme ho pro dalsi prohledavani
            }
            else {
                tmpNode = sKernelMem.Alloc<TFS_Tree_Node>();
                strncpy(tmpNode->name, tmpName, MaxFilenameLength);
                tmpNode->parent = node;
                tmpNode->children = nullptr;
                tmpNode->driver_idx = NoFilesystemDriver; // zatim - mozna se neco dole zmeni
                tmpNode->isDirectory = true;
                tmpNode->next = node->children;
                node->children = tmpNode;

                node = tmpNode;
            }
        }

        // 'node' obsahuje uzel posledniho clanku mountpointu - tedy vlastni mountpoint

        // mountpoint nesmi byt uz zabrany
        if (node->driver_idx != NoFilesystemDriver) {
            return;
        }

        node->driver_idx = i; // timto predavame veskere manipulace driveru

        // dame driveru vedet, ze jsme ho zaregistrovali
        ptr->driver->On_Register();
    }
}

IFile* CFilesystem::Open(const char* path, NFile_Open_Mode mode) {
    char tmpName[MaxFilenameLength];
    const char* mpPtr;

    int j;

    mpPtr = path;

    TFS_Tree_Node* node = &mRoot, *tmpNode = nullptr;

    while (mpPtr[0] != '\0') {
        for (j = 0; j < MaxPathLength && mpPtr[j] != '\0'; j++) {
            if (mpPtr[j] == ':' || mpPtr[j] == '/') {
                break;
            }

            tmpName[j] = mpPtr[j];
        }

        tmpName[j] = '\0';
        
        mpPtr += j;

        // jedna se o konec retezce?
        if (mpPtr[0] != '\0') {
            mpPtr += 1;
        }

        tmpNode = node->Find_Child(tmpName);
        if (tmpNode) {
            node = tmpNode;
        }

        if (tmpNode->driver_idx != NoFilesystemDriver) {
            return gFS_Drivers[tmpNode->driver_idx].driver->Open_File(mpPtr, mode);
        } else if (!tmpNode->isDirectory) {
            break;
        }
    }

    // zadny filesystem se tohoto uzlu "neujal" -> soubor neexistuje
    return nullptr;
}

void IFile::Wait_Enqueue_Current() {
    spinlock_lock(&mWait_Lock);

    TWaiting_Task* task = new TWaiting_Task;
    auto* curthread = sScheduler.Get_Current_Thread();
    task->pidtid = CThread_Manager::GetPIDTID(curthread); // zmena na PIDTID abychom jednoznacne identifikovali vlakno
    task->prev = nullptr;
    task->next = nullptr;

    if (!mWaiting_Tasks) {
        mWaiting_Tasks = task;  //kdyz nemame zadny cekajici task tak ten co prave enqueueujeme je zaroven zacatek i konec seznamu
        mWaiting_Tasks_End = task;
    } else {    // enqueue-back
        mWaiting_Tasks_End->next = task;
        task->prev = mWaiting_Tasks_End;
        mWaiting_Tasks_End = task;
    }

    spinlock_unlock(&mWait_Lock);
}

bool IFile::Wait_Dequeue_Thread(PIDTID pidtid) {
    spinlock_lock(&mWait_Lock);

    TWaiting_Task* itr = mWaiting_Tasks;
    bool found = false;

    while (itr) {
        if (itr->pidtid == pidtid) { // zmena na PIDTID abychom jednoznacne identifikovali vlakno
            found = true;

            if (itr->prev) {
                itr->prev->next = itr->next;
            } else {
                mWaiting_Tasks = itr->next;
            }

            if (itr->next) {
                itr->next->prev = itr->prev;
            }

            delete itr;
            break;
        }
        itr = itr->next;
    }

    spinlock_unlock(&mWait_Lock);
    return found;
}

uint32_t IFile::Notify(uint32_t count) {

    spinlock_lock(&mWait_Lock); //zamknu zamek tohoto konkretniho souboru

    TWaiting_Task* tmp;
    TWaiting_Task* itr = mWaiting_Tasks;
    while (itr && itr->next) { //najdu zacatek fronty cekajicich vlaken
        itr = itr->next;
    }

    uint32_t notified_count = 0;
    while (itr && notified_count < count) { //dokud nenotifikuju count vlaken
        sThread_Manager.Notify_Thread(itr->pidtid, this); //notifikuju v procesu vsechna vlakna ktera nad timto souborem cekaji

        tmp = itr; 

        itr = itr->prev;  //a posouvam se ve fronte dal
        delete tmp;
        notified_count++;

        if (itr) {
            itr->next = nullptr;
        } else {
            mWaiting_Tasks = nullptr;
            break;
        }
    }

    spinlock_unlock(&mWait_Lock);

    return notified_count;
}
