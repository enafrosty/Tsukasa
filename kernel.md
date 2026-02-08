# Kernel execution and memory assumptions

## Stack

- **Size:** 16 KiB (16384 bytes), defined in `boot.s` between `stack_bottom` and `stack_top`.
- **Alignment:** `stack_top` is 16-byte aligned. Before `call kernel_main`, `eax` and `ebx` are pushed (8 bytes), so `esp` is 8-byte aligned at C entry. Sufficient for 32-bit code; document if you later rely on 16-byte alignment (e.g. SSE or ABI).
- **Usage:** Single kernel stack. All code (kernel_main, IDT exception handlers, and any future code that runs before a scheduler) uses this stack. When adding ISRs or multiple tasks, document where their stacks live (e.g. per-task stacks or a dedicated ISR stack).

## Execution flow

- **Entry:** GRUB loads the kernel at 1 MiB, jumps to `_start` in `boot.s`. Stack pointer is set to `stack_top`, then Multiboot magic (eax) and info (ebx) are passed to `kernel_main(uint32_t magic, uint32_t info)`.
- **No heap:** There is no dynamic allocator; no heap allocations or frees.
- **IDT:** A minimal IDT is set up early in `kernel_main` so CPU exceptions are caught and reported (e.g. to VGA) instead of triple faulting.

## When adding tasks or ISRs

- Give each task its own stack region (or use a single shared stack only if you never context-switch).
- Disable interrupts around context switch and avoid shared mutable state without explicit synchronization.
- Document stack placement and size for ISRs and tasks in this file or in `boot.s` comments.

---

## Task / process switching (future design)

When you add a scheduler, follow this design to avoid subtle crashes:

1. **One stack per task.** Each task has its own stack (or region). The context switch saves the current stack pointer and restores the next task’s stack pointer. Do not share one stack across tasks.

2. **Interrupt masking during context switch.** Disable interrupts (e.g. `cli`) for the duration of the switch. Re-enable after the new context is loaded (e.g. before `iret` or when returning to the new task). This avoids races where an interrupt fires mid-switch and uses a half-updated state.

3. **No shared mutable state without synchronization.** Shared data (e.g. VGA cursor, task queue, ready list) must be updated only when interrupts are disabled or with an explicit locking scheme. Do not hold a lock across a context switch.

4. **Audit the switch path.** The switch path must save and restore all callee-saved state (e.g. general-purpose registers, segment registers if changed) and the stack pointer. Test with multiple tasks and under interrupts to ensure no register or stack is leaked or corrupted.
