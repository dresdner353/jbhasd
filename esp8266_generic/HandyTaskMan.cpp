// HandyTaskMan.cpp
// Place witty description here and GPL

#include <Arduino.h>
#include "HandyTaskMan.h"

// Function set_run_state
// Changes run state to the desired 32-bit 
// pattern. This will directly influence the tasks 
// that are then called
void HandyTaskMan::set_run_state(uint32_t new_state)
{
    log("HandyTaskMan::set_run_state(0x%08X)", 
        new_state);
    run_state = new_state;
}


// Function get_run_state
// returns current run state
uint32_t HandyTaskMan::get_run_state(void)
{
    log("HandyTaskMan::get_run_state()");
    log("Run State: 0x%08X", run_state);
    return run_state;
}


// Function loop_task_alloc
// allocates interal loop task struct
struct loop_task* HandyTaskMan::loop_task_alloc(const char *name)
{
    struct loop_task *loop_task;

    loop_task = (struct loop_task*) malloc(sizeof(struct loop_task));
    if (!name || *name == '\0') {
        // force a name if omitted or empty
        name = "Unknown";
    }
    loop_task->name = strdup(name);

    return loop_task;
}

// Function loop_task_free
// frees up loop task
void HandyTaskMan::loop_task_free(struct loop_task* loop_task)
{
    if (loop_task->name) {
        free(loop_task->name);
    }
    free(loop_task);
}

// Function init
// initialises task manager 
// called via constructor but can be manually invoked
// to wipe the slate clean in terms of callbacks and current
// run state
void HandyTaskMan::init(void)
{
    struct loop_task *loop_task;

    // init sleep time 
    sleep_time = 0;

    set_run_state(HTM_RUN_STATE_STOPPED);

    // Task list, either create it or empty it
    if (!task_list) {
        task_list = loop_task_alloc("head");
        HTM_LIST_SELFLINK(task_list);
    }
    else {
        // Free up everything except list head
        while (HTM_LIST_NEXT(task_list) != task_list) {
            loop_task = HTM_LIST_NEXT(task_list);
            HTM_LIST_REMOVE(loop_task);
            loop_task_free(loop_task);
        }
    }
}


// Function add_task
// Adds a desired callback function based on the 
// runstate mask of compatible states and a desired interval
// The mame acts as a unique key.. no two tasks can share the same name
// regardless of the other values (callback function, mask and interval)
void HandyTaskMan::add_task(const char *name,
                            uint32_t runstate_mask,
                            uint32_t call_interval,
                            void (*fp)(void))
{
    struct loop_task *loop_task;

    log("HandyTaskMan::add_task()");
    log("  Name:%s", name);
    log("  Run State mask:0x%08X", runstate_mask);
    log("  Call Interval %ums", call_interval);

    // Remove any existing task with the same name 
    // Allows for a re-entrant usage
    remove_task(name);

    loop_task = loop_task_alloc(name);
    loop_task->runstate_mask = runstate_mask;
    loop_task->call_interval = call_interval;
    loop_task->fp = fp;
    loop_task->last_call = 0;
    loop_task->num_calls = 0;
    loop_task->cpu_time = 0;

    HTM_LIST_INSERT(task_list, loop_task);
}


// Function remove_task
// Removes desired task by name
void HandyTaskMan::remove_task(const char *name)
{
    struct loop_task *loop_task, *deleted_task;

    log("HandyTaskMan::remove_task()");
    log("  Name:%s", name);

    loop_task = HTM_LIST_NEXT(task_list);
    while (loop_task != task_list) {

        // point at current entry and 
        // skip to next
        deleted_task = loop_task;
        loop_task = HTM_LIST_NEXT(loop_task);

        if (!strcmp(deleted_task->name, name)) {
            HTM_LIST_REMOVE(deleted_task);
            loop_task_free(deleted_task);
            log("found & deleted task");
        }
    }
}


// Function nudge
// iterates through register of tasks
// and calls those that have reached the qualifying
// run state and callback interval
void HandyTaskMan::nudge(void)
{
    struct loop_task *loop_task;
    uint32_t now;

    if (run_state == HTM_RUN_STATE_STOPPED) {
        // not running, nothing to do
        return;
    }

    for (loop_task = HTM_LIST_NEXT(task_list);
         loop_task != task_list;
         loop_task = HTM_LIST_NEXT(loop_task)) {
        now = millis();

        // Check if mode mask matches current mode
        // and that interval since last call >= delay between calls
        // this is unsigned arithmetic and will nicely handle a 
        // wrap around of millis()
        if ((loop_task->runstate_mask & run_state) &&
            now - loop_task->last_call 
            >= loop_task->call_interval) {

            // Record call time
            loop_task->last_call = now;

            // Call function
            loop_task->fp();

            // Calculate call stats
            now = millis();
            loop_task->cpu_time += (now - loop_task->last_call);
            loop_task->num_calls++;
        }
    }
}

// Function sleep
// provides a millisecond sleep based on 
// calculating the smallest interval from now to 
// the next scheduled task
void HandyTaskMan::sleep(void)
{
    struct loop_task *loop_task;
    uint32_t now = millis();
    uint32_t sleep_interval = 1000;
    uint32_t time_to_next_call;

    if (run_state == HTM_RUN_STATE_STOPPED) {
        // not running, nothing to do
        return;
    }

    for (loop_task = HTM_LIST_NEXT(task_list);
         loop_task != task_list;
         loop_task = HTM_LIST_NEXT(loop_task)) {

        if (loop_task->runstate_mask & run_state) {
            // time in msecs to next call
            if (loop_task->call_interval == 1) {
                // bypass for 1ms interval (argb/rgb)
                sleep_interval = 0;
            }
            else {
                time_to_next_call = loop_task->last_call + loop_task->call_interval - now;
                if (time_to_next_call < sleep_interval) {
                    sleep_interval = time_to_next_call;
                }
            }
        }
    }

    // protect against overly long sleeps
    if (sleep_interval > 1000) {
        sleep_interval = 1000;
    }

    // skip for short periods
    // such as rgb/argb that use a 1msec loop
    if (sleep_interval == 0) {
        return;
    }

    // account for sleep and do the delay
    sleep_time += sleep_interval;
    delay(sleep_interval);
}

// Function set_logger
// sets optional external logging function
void HandyTaskMan::set_logger(void (*fp)(char *format, va_list args ))
{
    log_fp = fp;
}


// Function log
// internal log function that invokes external 
// logger if set
void HandyTaskMan::log(char *format, ... )
{
    va_list args;

    if (!log_fp) {
        return;
    }

    va_start(args, format);
    log_fp(format, args);
    va_end(args);
}


// Function log_stats
// Logs stats for all tasks including
// number of calls and acumulated CPU time.
// Then it clears down counters
void HandyTaskMan::log_stats(void)
{
    struct loop_task *loop_task;

    log("HandyTaskMan::log_stats()");
    for (loop_task = HTM_LIST_NEXT(task_list);
         loop_task != task_list;
         loop_task = HTM_LIST_NEXT(loop_task)) {

        if (loop_task->num_calls > 0) {
            log("  Task:%s Interval:%u Calls:%u CpuTime:%u",
                loop_task->name,
                loop_task->call_interval,
                loop_task->num_calls,
                loop_task->cpu_time);

            loop_task->num_calls = 0;
            loop_task->cpu_time = 0;
        }
    }

    // log sleep time and reset
    log("  SleepTime:%u", sleep_time);
    sleep_time = 0;
}


// Constructor
HandyTaskMan::HandyTaskMan(void)
{
    init();
}


// Desctructor
HandyTaskMan::~HandyTaskMan(void)
{
    init();
    loop_task_free(task_list);
}
