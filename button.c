#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define IN 0
#define OUT 1

#define LOW 0
#define HIGH 1

#define BUFFER_MAX 3
#define VALUE_MAX 40
#define DIRECTION_MAX 35

typedef struct button {
    int pin;
    int pout;
    int polling_rate;
    void *(*onLongClick)();
    void *(*onPressDown)();
    void *(*onPressUp)();
} BUTTON;

// get GPIO control from kernel
static int GPIOExport(int pin){
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd; // file descriptor

    fd = open("/sys/class/gpio/export", O_WRONLY); // open /sys/class/gpio/export directory with write only permission
    if (-1 == fd){ // if it fails, return -1
        return -1;
    }
    
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written); // write file
    close(fd); // close file descriptor
    return 0;
}

// return GPIO contorl to kernel
static int GPIOUnexport(int pin){
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd; // file descriptor

    fd = open("/sys/class/gpio/unexport", O_WRONLY); // open /sys/class/gpio/unexport directory with write only permission
    if (-1 == fd) // if it fails, return -1
        return -1;

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written); // write file
    close(fd); // close file descriptor
    return 0;
}

// decide specific GPIO's input or output
static int GPIODirection(int pin, int dir) {
    static const char s_directions_str[] = "in\0out";

    char path[DIRECTION_MAX] = "/sys/class/gpio/gpio%d/direction"; // initializes path variable with /sys/class/gpio/gpio[gpio number]/direction
    int fd; // file descriptor

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);

    fd = open(path, O_WRONLY); // open path with write only permission
    if (-1 == fd) // if it fails, return -1
        return -1;

    if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) { // writing value. if IN, select 0 for 'in', or select 3 for 'out'
        close(fd); // if it fails, closes file descriptor and then returns -1
        return -1;
    }

    close(fd); // if it completes this function, closes file descriptor and then returns 0
    return 0;
}

// read values from GPIO
static int GPIORead(int pin){
    char path[VALUE_MAX];
    char value_str[3];
    int fd; // file descriptor

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY); // open path with read only permission
    if (-1 == fd) // if it fails, return -1
        return -1;

    if (-1 == read(fd, value_str, 3)){ // reading value (maximum 3 bytes) from file descriptor
        close(fd); // if it fails, closes file descriptor and then returns -1
        return -1;
    }

    close(fd); // if it completes this function, closes file descriptor and then returns integer converted from string
    return atoi(value_str);
}

// write values from GPIO
static int GPIOWrite(int pin, int value){
    static const char s_values_str[] = "01";

    char path[VALUE_MAX];
    int fd; // file descriptor

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY); // open path with write only permission
    if (-1 == fd) // if it fails, return -1
        return -1;

    if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)){ // writing 1 byte value. if LOW, select 0, or select 1
        close(fd); // if it fails, closes file descriptor and then returns -1
        return -1;
    }

    close(fd); // if ti completes this function, closes file descriptor and then returns 0
    return 0;
}

// called when the thread is canceled
void dispose(void *arg){
    BUTTON btn = *(BUTTON*)arg;

    // disable GPIO pin
    while (-1 == GPIOUnexport(btn.pout)) // if it fails, excutes till works
        GPIOUnexport(btn.pin);

    // disable GPIO pout
    while (-1 == GPIOUnexport(btn.pin)) // if it fails, excutes till works
        GPIOUnexport(btn.pout);
}

// processes the events of button by polling
void *event_routine(void *arg){
    double time; // for saving the subtraction of start time and end time
    clock_t start_t = 0.0, pressed_t = 0.0; // initializing clock_t type start time and button pressed time variables with 0.0
    // for checking if longCkick(), from main function and described as call-back function on button structure, is executed or not
    int flag = 0;

    // initializes BUTTON type, described on above, btn variable with the parameter from initButton
    BUTTON btn = *(BUTTON*)arg;
    
    
    // call GPIOExport function to enable GPIO pout
    while (-1 == GPIOExport(btn.pout)) // when it fails, executes till works
        GPIOExport(btn.pout);

    // call GPIOExport function to enable GPIO pin
    while (-1 == GPIOExport(btn.pin)) // when it fails, executes till works
        GPIOExport(btn.pin);

    // call GPIODirection function to set GPIO direction of pout
    while (-1 == GPIODirection(btn.pout, OUT)) // when it fails, executes till works
        GPIODirection(btn.pout, OUT);

    // call GPIODirection function to set GPIO direction of pin
    while (-1 == GPIODirection(btn.pin, IN)) // when it fails, executes till works
        GPIODirection(btn.pin, IN);


    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_cleanup_push(dispose, &btn.pin);

    // button event occurs infinitely till onLongClick is called twice
    while(1){
        // call GPIOWrite function to write GPIO response with pout
        // this function will not be called for pin, because the writing is needed when the button is pressed only
        while (-1 == GPIOWrite(btn.pout, 1)) // when it fails, executes till works
            GPIOWrite(btn.pout, 1);

        // check if the button is pressed or not
        if (GPIORead(btn.pin) == 0){
            start_t = clock(); // save the start time of pressing
            btn.onPressDown(); // call onPressDown function

            while (GPIORead(btn.pin) == 0){ // check if the button is released or not for pressed time
                while (!flag){ // check if the onLongClick function is executed or not. Since it doesn't have to be executed in multiple times
                    if (GPIORead(btn.pin) != 0) // check if the button is pressed or not, even though the onLongClick function is not executed
                        break;
                    pressed_t = clock(); // save present time: it works only when the button is pressed
                    time = (double)(pressed_t - start_t) / CLOCKS_PER_SEC * 1000; // how long is the button pressed in ms
                    if (time >= (double)(800)){ // when the button pressed time is over 800 ms
                        btn.onLongClick(); // executes onLongClick function
                        flag = 1; // make flag 1 to end this infinite loop 
                    }
                }
            }

            flag = 0; // reset flag for next iteration

        }

        if (GPIORead(btn.pin) == 1){ // check if the button is released or not
            btn.onPressUp(); // executes onPressUp function
            while (GPIORead(btn.pin) == 1); // check if the button is released or not
        }

        // usleep works in micro sec. e.g. usleep(1) = 1/1000000
        // in case of this assignment, the polling rate is written as hz. that means this program has to check given value times in 1 sec
        // sec = 1/hz
        // in this program, hence, 1 / given polling rate * 1000000 for the parameter of usleep
        usleep(1/btn.polling_rate*1000000);

    }

    pthread_cleanup_pop(0);
}


pthread_t *initButton(BUTTON button){
    // allocates memory to variables
    pthread_t *t = (pthread_t *)malloc(sizeof(pthread_t)); // thread
    BUTTON *btn = (BUTTON *)malloc(sizeof(BUTTON)); // button

    *btn = button;

    // when the thread is not created
    if (pthread_create(t, NULL, event_routine, btn) != 0){
        // return allocated memory
        free(t);
        free(btn);
        return NULL;
    }

    return t;
}
