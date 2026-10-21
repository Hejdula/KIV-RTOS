#include "thread_manager.h"



inline TThread_Struct *CThread_Manager::Create_Thread()
{
    auto* newThread = sKernelMem.Alloc<TThread_Struct>();

}

void CThread_Manager::Create_First_Thread(TProcess_Struct* process, unsigned char* elf_file_data, unsigned int elf_file_length, bool is_system)
{
    return ;
}
