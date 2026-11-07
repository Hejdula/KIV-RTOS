#include <stdfile.h>
#include <stdstring.h>
#include <stdmutex.h>

#include <drivers/bridges/uart_defs.h>


inline constexpr const int THREAD_COUNT = 4;
inline constexpr const char* MESSAGE = "Hello, I'm thread 0!\n";
volatile uint32_t uart_file;
volatile uint32_t trng_file;

inline constexpr const uint32_t MAX_SLEEP = 0x400;
inline constexpr const uint32_t MIN_SLEEP = 0x200;

volatile mutex_t uart_mutex;


inline static void fputs(uint32_t file, const char* string) { // zapujceno z logger tasku
    write(file, string, strlen(string));
}



void uart_thread(uint32_t name) {
    char msg[strlen(MESSAGE)];
    strncpy(msg, MESSAGE, strlen(MESSAGE));
    msg[strlen(MESSAGE) - 3] = name + '0'; // lehce dirty zapis predaneho argumentu
    uint32_t sleep_timer = 0;
    //set_task_deadline(0x100);
    while (true)
    {
        mutex_lock(uart_mutex);

        read(trng_file, reinterpret_cast<char*>(&sleep_timer), sizeof(sleep_timer));
        if (sleep_timer > MAX_SLEEP) { // pokud je doba spanku moc dlouha, tak ji zkratime pod maximalni pripustnou mez, a zaroven zachovame nahodu
            sleep_timer %= MAX_SLEEP; // to je asi poprve a naposledy co nekdo pouzil tenhle operator
        }
        if (sleep_timer < MIN_SLEEP) { // pokud je doba spanku moc kratka, tak ji prodlouzime nad minimalni pripustnou mez, a zaroven zachovame nahodu
            sleep_timer += MIN_SLEEP; 
        }
        //debug
        //sleep_timer = MAX_SLEEP;
        char debug[10];
        itoa(sleep_timer, debug, 10);
        fputs(uart_file, debug);
        fputs(uart_file, msg);
        
        mutex_unlock(uart_mutex);
        set_task_deadline(0x4000 + sleep_timer);
        sleep(sleep_timer);
    }
    
    
}

int main(int argc, char** argv) {
    uart_file = open("DEV:uart/0", NFile_Open_Mode::Write_Only);

    TUART_IOCtl_Params params;
    params.baud_rate = NUART_Baud_Rate::BR_115200;
    params.char_length = NUART_Char_Length::Char_8;
    ioctl(uart_file, NIOCtl_Operation::Set_Params, &params);
    
    trng_file = open("DEV:trng", NFile_Open_Mode::Read_Only);

    uart_mutex = mutex_create("uart_mutex");
    

    fputs(uart_file, "UART task starting!\n");

    for (int i = 0; i < THREAD_COUNT; i++) //zalozime vlakna, nezajimaji nas jejich TIDy
        thread(reinterpret_cast<void* (*)(void*)>(&uart_thread), reinterpret_cast<void*>(i+1));


    while (true) {
        sleep(0x100000); // toto vlakno bude jenom spat
    }
}
