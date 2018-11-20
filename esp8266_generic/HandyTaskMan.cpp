// HandyTaskMan.cpp
// Place witty description here and GPL

#include <Arduino.h>
#include "HandyTaskMan.h"

void HandyTaskMan::set_run_state(uint32_t new_state)
{
    log("HandyTaskMan::set_run_state(0x%08X)", 
        new_state);
    run_state = new_state;
}

uint32_t HandyTaskMan::get_run_state(void)
{
    log("HandyTaskMan::get_run_state()");
    log("Run State: 0x%08X", run_state);
    return run_state;
}

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

void HandyTaskMan::loop_task_free(struct loop_task* loop_task)
{
    if (loop_task->name) {
        free(loop_task->name);
    }
    free(loop_task);
}

void HandyTaskMan::init(void)
{
    struct loop_task *loop_task;

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

    loop_task = loop_task_alloc(name);
    loop_task->runstate_mask = runstate_mask;
    loop_task->call_interval = call_interval;
    loop_task->fp = fp;
    loop_task->last_call = 0;
    loop_task->num_calls = 0;
    loop_task->cpu_time = 0;

    HTM_LIST_INSERT(task_list, loop_task);
}

void HandyTaskMan::remove_task(const char *name,
                               uint32_t runstate_mask)
{
    struct loop_task *loop_task, *deleted_task;

    log("HandyTaskMan::remove_task()");
    log("  Name:%s", name);
    log("  Run State mask:0x%08X", runstate_mask);

    loop_task = HTM_LIST_NEXT(task_list);
    while (loop_task != task_list) {
        if (!strcmp(loop_task->name, name) &&
            (loop_task->runstate_mask == runstate_mask)) {
            deleted_task = loop_task;
            loop_task = HTM_LIST_NEXT(loop_task);
            HTM_LIST_REMOVE(deleted_task);
            loop_task_free(deleted_task);
            log("found & deleted task");
        }
    }
}

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

void HandyTaskMan::set_logger(void (*fp)(char *format, va_list args ))
{
    log_fp = fp;
}

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

void HandyTaskMan::log_stats(void)
{
    struct loop_task *loop_task;

    log("HandyTaskMan::log_stats()");
    for (loop_task = HTM_LIST_NEXT(task_list);
         loop_task != task_list;
         loop_task = HTM_LIST_NEXT(loop_task)) {

        if (loop_task->num_calls > 0) {
            log("  Task:%s Calls:%u CpuTime:%u",
                loop_task->name,
                loop_task->num_calls,
                loop_task->cpu_time);

            loop_task->num_calls = 0;
            loop_task->cpu_time = 0;
        }
    }
}

HandyTaskMan::HandyTaskMan(void)
{
    init();
}

HandyTaskMan::~HandyTaskMan(void)
{
    init();
    loop_task_free(task_list);
}


