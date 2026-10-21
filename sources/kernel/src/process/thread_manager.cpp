#include "thread_manager.h"

CThread_Manager sThread_Manager;

inline TThread_Struct *CThread_Manager::Create_Thread()
{
    auto* newThread = sKernelMem.Alloc<TThread_Struct>();

}

TThread_Struct *CThread_Manager::Create_First_Thread(unsigned char *elf_file_data, unsigned int elf_file_length, bool is_system)
{
    return nullptr;
}
