#include <stdstring.h>
#include <stdfile.h>

#include <drivers/gpio.h>
#include <process/process_manager.h>



int main(int argc, char** argv) {
    
    uint32_t REDLED_file = open("DEV:gpio/23", NFile_Open_Mode::Write_Only);


    bool fast = true;
    bool on = true;

    set_task_deadline(fast ? 0x1000 : 0x4000);

    while (true) {
        write(REDLED_file, "1", 2);
        sleep(fast ? 0x400 : 0x1200, fast ? 0x1000 : 0x4000);
        write(REDLED_file, "0", 2);
        sleep(fast ? 0x400 : 0x1200, fast ? 0x1000 : 0x4000);
    }

    return 0;
}
