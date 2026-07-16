#ifndef SMP_H
#define SMP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct cpu_state {
    struct cpu_state *self;
    uint32_t cpu_id;
    uint32_t lapic_id;
    uint64_t kernel_stack;
    void *kernel_stack_alloc;
    volatile bool online;
} cpu_state_t;

void smp_init_bsp(void);

struct limine_smp_response;
uint32_t smp_init(struct limine_smp_response *smp_resp);
uint32_t smp_this_cpu_id(void);
uint32_t smp_cpu_count(void);
cpu_state_t *smp_get_cpu(uint32_t cpu_id);
uint32_t smp_get_lapic_id(uint32_t cpu_id);

#endif /* SMP_H */
