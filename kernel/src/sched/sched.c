#include <sched/sched.h>
#include <arch/smp/smp.h>
#include <dev/serial/serial.h>
#include <dev/timer/rtc/rtc.h>

bool sched_initialised = false;
u64 sched_pid = 0;

void sched_wrapper(u64 func) {
    ((u64(*)())func)();
    lock();
    this_cpu()->current_proc->state = PROC_DEAD;
    unlock();
    for (;;) {
        __asm__ ("hlt");
    }
}

process* sched_new_proc(void* func, u64 cpu_id) {
    lock();

    if (get_cpu(cpu_id) == NULL) {
        serial_printf("Sched error: Non-existent CPU %lx\n", cpu_id);
        return NULL;
    }

    process* proc = (process*)kmalloc(sizeof(process));

    proc->PID = sched_pid;
    proc->pm = vmm_new_pm();
    proc->state = PROC_RUNNING;
    proc->cpu_id = cpu_id;

    proc->regs.rip = (u64)sched_wrapper;
    proc->regs.rdi = (u64)func;
    proc->regs.cs  = 0x28;
    proc->regs.ss  = 0x30;
    proc->regs.rflags = 0x202;

    char* stack = (char*)kmalloc(8 * PAGE_SIZE); // 32 kb
    proc->stack_addr = (u64)(stack);
    proc->regs.rsp = (u64)(stack + (PAGE_SIZE * 8));

    get_cpu(cpu_id)->proc_list[get_cpu(cpu_id)->proc_size] = proc;
    sched_pid++;
    get_cpu(cpu_id)->proc_size++;
    unlock();

    return proc;
}

process* sched_new_elf(void* elf, u64 cpu_id) {
    lock();

    if (get_cpu(cpu_id) == NULL) {
        serial_printf("Sched error: Non-existent CPU %lx\n", cpu_id);
        return NULL;
    }

    process* proc = (process*)kmalloc(sizeof(process));

    proc->PID = sched_pid;
    proc->pm = vmm_new_pm();
    proc->state = PROC_RUNNING;
    proc->cpu_id = cpu_id;

    u64 entry_point = elf_load(elf, proc->pm);
    if (entry_point == -1) {
        serial_printf("Couldn't open elf!\n");
        kfree(proc);
        unlock();
        return NULL;
    }

    proc->regs.rip = (u64)sched_wrapper;
    proc->regs.rdi = (u64)entry_point;
    proc->regs.cs  = 0x28;
    proc->regs.ss  = 0x30;
    proc->regs.rflags = 0x202;

    char* stack = (char*)kmalloc(8 * PAGE_SIZE); // 32 kb
    proc->regs.rsp = (u64)(stack + (PAGE_SIZE * 8));

    get_cpu(cpu_id)->proc_list[get_cpu(cpu_id)->proc_size] = proc;
    sched_pid++;
    get_cpu(cpu_id)->proc_size++;
    unlock();

    return proc;
}

void sched_kill() {
    lock();
    this_cpu()->current_proc->state = PROC_DEAD;
    unlock();
}

process* sched_get_proc(u64 pid) {
    cpu_info* cpu;
    for (u64 i = 0; i < smp_cpu_count; i++) {
        cpu = get_cpu(i);
        for (u64 j = 0; j < cpu->proc_size; j++)
            if (cpu->proc_list[j]->PID == pid)
                return cpu->proc_list[j];
    }
    return NULL;
}

void sched_remove_proc(u64 pid) {
    process* proc = sched_get_proc(pid);
    cpu_info* cpu = get_cpu(proc->cpu_id);

    if (proc == NULL) {
        serial_printf("Sched: Couldn't kill proc %lx not found.\n", pid);
        return;
    }

    vmm_destroy_pm(proc->pm);
    //kfree((void*)(proc->regs.rsp - (PAGE_SIZE * 8)));
    // Page faults me for some obscure reason ^
    kfree(proc);

    cpu->proc_size--;
    serial_printf("Killed process %ld in CPU %ld.\n", pid, cpu->lapic_id);
}

u32 sched_get_cpu_load(cpu_info* cpu) {
    u64 current_time = rtc_get_unix();
    u64 total_time_since_last_calc = current_time - cpu->total_time;
    if (total_time_since_last_calc == 0) {
        return 0;
    }

    u64 busy_time = total_time_since_last_calc - (current_time - cpu->last_idle_time);

    u32 cpu_load = (u32)((busy_time * 100ULL) / total_time_since_last_calc);

    cpu->total_time = current_time;
    cpu->last_idle_time = current_time;

    return cpu_load;
}

void sched_update_usage(cpu_info* cpu) {
    u64 current_time = rtc_get_unix();  // Replace with the actual function to get the current time
    cpu->total_time = current_time;
    cpu->last_idle_time = current_time;
}

void sched_switch(registers* regs) {
    lock();
    cpu_info* cpu = this_cpu();
    sched_update_usage(cpu);
    cpu->current_proc->regs = *regs;
    cpu->current_proc = cpu->proc_list[cpu->proc_idx];
    *regs = cpu->current_proc->regs;
    vmm_switch_pm(cpu->current_proc->pm);

    cpu->proc_idx++;
    if (cpu->proc_idx == cpu->proc_size) {
        cpu->proc_idx = 0;
    }
    if (cpu->proc_list[cpu->proc_idx]->state == PROC_DEAD) {
        sched_remove_proc(cpu->proc_list[cpu->proc_idx]->PID);
        cpu->proc_idx = 0;
    }
    unlock();
}

void lock() {
    if (!sched_initialised) return;
    while (__atomic_test_and_set(&(this_cpu()->lock), __ATOMIC_ACQUIRE));
}

void unlock() {
    if (!sched_initialised) return;
    __atomic_clear(&(this_cpu()->lock), __ATOMIC_RELEASE);
}

void sched_idle() {
    sched_update_usage(this_cpu());
    while (true) {
        __asm__ volatile ("nop");
    }
}

void sched_init() {
    sched_initialised = true;
    cpu_info* cpu = this_cpu();
    sched_new_proc(sched_idle, lapic_get_id());
    cpu->current_proc = cpu->proc_list[0];
}