#include <stdfile.h>
#include <stdstring.h>

//#include <drivers/gpio.h>
#include <oled.h>
//#include <process/process_manager.h>

inline constexpr const int THREAD_COUNT = 8;

volatile uint32_t counters[THREAD_COUNT]; //nemusime se bat problemu zpusobenych cachovanim pameti - jsme na uniprocesorovem systemu



void counter_thread(uint32_t speed_index) {
    int speed = speed_index % THREAD_COUNT;
    speed++; // rychlost musim shiftnout o jeden abych nevyrobil nulu
    int index = speed_index / THREAD_COUNT;

    counters[index] = 0;
    set_task_deadline(0x100);
    while (true) //pro prednasejici predmetu KKY/SWAP " for(;;) {} "
    {
        counters[index]++;
        if (counters[index] >= 10) counters[index] = 0;
        sleep(0x200*speed);
    }
    
}

int main(int argc, char** argv) {
    for (int i = 0; i < THREAD_COUNT; i++) //zalozime osm vlaken, nezajimaji nas jejich TIDy
        thread(reinterpret_cast<void* (*)(void*)>(&counter_thread), reinterpret_cast<void*>(i*8+i));

    COLED_Display disp("DEV:oled");
    disp.Clear(false);
    disp.Put_String(2, 2, "Counter task init...");
    disp.Flip();
    char disp_data[strlen("Counters: 0 0 0 0 0 0 0 0")];
    strncpy(disp_data, "Counters: 0 0 0 0 0 0 0 0", strlen("Counters: 0 0 0 0 0 0 0 0"));
    set_task_deadline(0x100);
    while (true) {
        sleep(0x100);
        for (int i = 0; i < THREAD_COUNT; i++) {
            disp_data[10 + i*2] = counters[i] + '0';
        }
        //disp.Clear(false);
        disp.Put_String(2,2, disp_data);
        disp.Flip();
    }
}
