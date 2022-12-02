// HandyTaskMan.h
// Place witty description here and GPL

#ifndef HandyTaskMan_H_
#define HandyTaskMan_H_

#include <Arduino.h>

// Linked-list macros

#define HTM_LIST_SELFLINK(head) { \
    (head)->prev = (head); \
    (head)->next = (head); \
}

#define HTM_LIST_EMPTY(head) (head->next == head && head->prev == head)

#define HTM_LIST_INSERT(head, node) { \
    (node)->next = (head); \
    (node)->prev = (head)->prev; \
    (node)->prev->next = node; \
    (head)->prev = (node); \
}

#define HTM_LIST_REMOVE(node) { \
    (node)->prev->next = (node)->next; \
    (node)->next->prev = (node)->prev; \
    (node)->next = NULL; \
    (node)->prev = NULL; \
}

#define HTM_LIST_NEXT(node) (node)->next

#define HTM_LIST_PREV(node) (node)->prev

// Run State
// Each bit assigned to a separate state
#define HTM_RUN_STATE_STOPPED 0x00000000  
#define HTM_RUN_STATE_00      0x00000001 
#define HTM_RUN_STATE_01      0x00000002
#define HTM_RUN_STATE_02      0x00000004
#define HTM_RUN_STATE_03      0x00000008
#define HTM_RUN_STATE_04      0x00000010
#define HTM_RUN_STATE_05      0x00000020
#define HTM_RUN_STATE_06      0x00000040
#define HTM_RUN_STATE_07      0x00000080
#define HTM_RUN_STATE_08      0x00000100
#define HTM_RUN_STATE_09      0x00000200
#define HTM_RUN_STATE_10      0x00000400
#define HTM_RUN_STATE_11      0x00000800
#define HTM_RUN_STATE_12      0x00001000
#define HTM_RUN_STATE_13      0x00002000
#define HTM_RUN_STATE_14      0x00004000
#define HTM_RUN_STATE_15      0x00008000
#define HTM_RUN_STATE_16      0x00010000
#define HTM_RUN_STATE_17      0x00020000
#define HTM_RUN_STATE_18      0x00040000
#define HTM_RUN_STATE_19      0x00080000
#define HTM_RUN_STATE_20      0x00100000
#define HTM_RUN_STATE_21      0x00200000
#define HTM_RUN_STATE_22      0x00400000
#define HTM_RUN_STATE_23      0x00800000
#define HTM_RUN_STATE_24      0x01000000
#define HTM_RUN_STATE_25      0x02000000
#define HTM_RUN_STATE_26      0x04000000
#define HTM_RUN_STATE_27      0x08000000
#define HTM_RUN_STATE_28      0x10000000
#define HTM_RUN_STATE_29      0x20000000
#define HTM_RUN_STATE_30      0x40000000
#define HTM_RUN_STATE_31      0x80000000
#define HTM_RUN_STATE_ALL     0xFFFFFFFF  


struct loop_task {
    struct loop_task *prev, *next;
    char *name;
    uint32_t runstate_mask;
    uint32_t call_interval;
    void (*fp)(void);
    uint32_t last_call;
    uint32_t num_calls;
    uint32_t cpu_time;
};
    
class HandyTaskMan
{
    public:
        HandyTaskMan();
        ~HandyTaskMan();

        void init(void);

        void set_run_state(uint32_t run_state);

        uint32_t get_run_state(void);

        void nudge(void);

        void sleep(void);

        void add_task(const char *name,
                      uint32_t runstate_mask,
                      uint32_t call_interval,
                      void (*fp)(void));

        void remove_task(const char *name);

        void set_logger(void (*fp)(char *format, va_list args));
        void log_stats(void);
                     
    private:
        uint32_t sleep_time;
        struct loop_task* loop_task_alloc(const char *name);
        void loop_task_free(struct loop_task* loop_task);

        uint32_t run_state = HTM_RUN_STATE_STOPPED;
        struct loop_task *task_list = NULL;

        void (*log_fp)(char *format, va_list args) = NULL;
        void log(char *format, ... );
};

#endif // HandyTaskMan_H_
