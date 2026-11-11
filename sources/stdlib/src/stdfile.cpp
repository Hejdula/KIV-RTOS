#include <stdfile.h>
#include <stdstring.h>

uint32_t getpid() {
    uint32_t pid;

    asm volatile("swi 0");
    asm volatile("mov %0, r0" : "=r" (pid));

    return pid;
}

uint32_t gettid() {
    uint32_t tid;

    asm volatile("swi 6");
    asm volatile("mov %0, r0" : "=r" (tid));

    return tid;
}

void terminate(int exitcode) {
    asm volatile("mov r0, %0" : : "r" (exitcode));
    asm volatile("swi 1");
}

void sched_yield() {
    asm volatile("swi 2");
}

uint32_t open(const char* filename, NFile_Open_Mode mode) {
    uint32_t file;

    asm volatile("mov r0, %0" : : "r" (filename));
    asm volatile("mov r1, %0" : : "r" (mode));
    asm volatile("swi 64");
    asm volatile("mov %0, r0" : "=r" (file));

    return file;
}

uint32_t read(uint32_t file, char* const buffer, uint32_t size) {
    uint32_t rdnum;

    asm volatile("mov r0, %0" : : "r" (file));
    asm volatile("mov r1, %0" : : "r" (buffer));
    asm volatile("mov r2, %0" : : "r" (size));
    asm volatile("swi 65");
    asm volatile("mov %0, r0" : "=r" (rdnum));

    return rdnum;
}

uint32_t write(uint32_t file, const char* buffer, uint32_t size) {
    uint32_t wrnum;

    asm volatile("mov r0, %0" : : "r" (file));
    asm volatile("mov r1, %0" : : "r" (buffer));
    asm volatile("mov r2, %0" : : "r" (size));
    asm volatile("swi 66");
    asm volatile("mov %0, r0" : "=r" (wrnum));

    return wrnum;
}

void close(uint32_t file) {
    asm volatile("mov r0, %0" : : "r" (file));
    asm volatile("swi 67");
}

uint32_t ioctl(uint32_t file, NIOCtl_Operation operation, void* param) {
    uint32_t retcode;

    asm volatile("mov r0, %0" : : "r" (file));
    asm volatile("mov r1, %0" : : "r" (operation));
    asm volatile("mov r2, %0" : : "r" (param));
    asm volatile("swi 68");
    asm volatile("mov %0, r0" : "=r" (retcode));

    return retcode;
}

uint32_t notify(uint32_t file, uint32_t count) {
    uint32_t retcnt;

    asm volatile("mov r0, %0" : : "r" (file));
    asm volatile("mov r1, %0" : : "r" (count));
    asm volatile("swi 69");
    asm volatile("mov %0, r0" : "=r" (retcnt));

    return retcnt;
}

NSWI_Result_Code wait(uint32_t file, uint32_t count, uint32_t notified_deadline) {
    NSWI_Result_Code retcode;

    asm volatile("mov r0, %0" : : "r" (file));
    asm volatile("mov r1, %0" : : "r" (count));
    asm volatile("mov r2, %0" : : "r" (notified_deadline));
    asm volatile("swi 70");
    asm volatile("mov %0, r0" : "=r" (retcode));

    return retcode;
}

uint32_t wait_all(uint32_t* files, uint32_t file_count, uint32_t notified_deadline)
{
    uint32_t file_handle;

    asm volatile("mov r0, %0" : : "r" (files));
    asm volatile("mov r1, %0" : : "r" (file_count));
    asm volatile("mov r2, %0" : : "r" (notified_deadline));
    asm volatile("swi 71");
    asm volatile("mov %0, r0" : "=r" (file_handle));

    return file_handle;
}

bool sleep(uint32_t ticks, uint32_t notified_deadline) {
    uint32_t retcode;

    asm volatile("mov r0, %0" : : "r" (ticks));
    asm volatile("mov r1, %0" : : "r" (notified_deadline));
    asm volatile("swi 3");
    asm volatile("mov %0, r0" : "=r" (retcode));

    return retcode;
}

uint32_t get_active_process_count() {
    const NGet_Sched_Info_Type req = NGet_Sched_Info_Type::Active_Process_Count;
    uint32_t retval;

    asm volatile("mov r0, %0" : : "r" (req));
    asm volatile("mov r1, %0" : : "r" (&retval));
    asm volatile("swi 4");

    return retval;
}

uint32_t get_active_thread_count() {
    const NGet_Sched_Info_Type req = NGet_Sched_Info_Type::Active_Thread_Count;
    uint32_t retval;

    asm volatile("mov r0, %0" : : "r" (req));
    asm volatile("mov r1, %0" : : "r" (&retval));
    asm volatile("swi 4");

    return retval;
}

uint32_t get_tick_count() {
    const NGet_Sched_Info_Type req = NGet_Sched_Info_Type::Tick_Count;
    uint32_t retval;

    asm volatile("mov r0, %0" : : "r" (req));
    asm volatile("mov r1, %0" : : "r" (&retval));
    asm volatile("swi 4");

    return retval;
}

void set_task_deadline(uint32_t tick_count_required) {
    const NDeadline_Subservice req = NDeadline_Subservice::Set_Relative;

    asm volatile("mov r0, %0" : : "r" (req));
    asm volatile("mov r1, %0" : : "r" (&tick_count_required));
    asm volatile("swi 5");
}

uint32_t get_task_ticks_to_deadline() {
    const NDeadline_Subservice req = NDeadline_Subservice::Get_Remaining;
    uint32_t ticks;

    asm volatile("mov r0, %0" : : "r" (req));
    asm volatile("mov r1, %0" : : "r" (&ticks));
    asm volatile("swi 5");

    return ticks;
}

// Po zavolani uzivatelske funkce zavola thread_exit_syscall
// Prvni ctyri argumenty jsou placeholders aby si argumenty fce vzal ze stacku 
// NOTE: musi byt v userspace, jinak by se proces snazil precict instrukci ze system memory a nastal prefetch abort
void thread_entry_wrapper(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t (*user_func)(void*), void* arg) {
    int exit_code = user_func(arg);

    asm volatile("mov %0, r0" : "=r" (exit_code));
    asm volatile("svc #9");
}

uint32_t thread(void* (*func)()){
    uint32_t retcode;

    asm volatile("mov r0, %0" : : "r" (func));
    asm volatile("mov r2, %0" : : "r" (&thread_entry_wrapper));
    asm volatile("swi 7");
    asm volatile("mov %0, r0" : "=r" (retcode));
    
    return retcode;
}

uint32_t thread(void* (*func)(void*), void* arg){
    uint32_t retcode;

    asm volatile("mov r0, %0" : : "r" (func));
    asm volatile("mov r1, %0" : : "r" (arg));
    asm volatile("mov r2, %0" : : "r" (&thread_entry_wrapper));
    asm volatile("swi 7");
    asm volatile("mov %0, r0" : "=r" (retcode));
    
    return retcode;
}

uint32_t join(uint32_t target) {
    uint32_t result;

    asm volatile("mov r0, %0" : : "r" (target));
    asm volatile("swi 8");
    asm volatile("mov %0, r0" : "=r" (result));

    return result;
}

const char Pipe_File_Prefix[] = "SYS:pipe/";

uint32_t pipe(const char* name, uint32_t buf_size) {
    char fname[64];
    int name_len = strlen(name);
    strncpy(fname, Pipe_File_Prefix, sizeof(Pipe_File_Prefix));
    strncpy(fname + sizeof(Pipe_File_Prefix) - 1, name, name_len);

    int ncur = sizeof(Pipe_File_Prefix) + name_len - 1;

    fname[ncur++] = '#';

    itoa(buf_size, &fname[ncur], 10);

    return open(fname, NFile_Open_Mode::Read_Write);
}
