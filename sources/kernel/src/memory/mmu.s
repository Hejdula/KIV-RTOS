.section .text

.global mmu_clean_dcache_entry
mmu_clean_dcache_entry:
    ;@ r0 contains the kernel space address of the page table entry pointer
    mcr p15, 0, r0, c7, c10, 1          ;@ Clean only the provided data cache line
    bx  lr

.global mmu_invalidate_cache
mmu_invalidate_cache:
    mov r1, #0
    mcr p15, 0, r1, c7, c7, 0
    bx lr

.global mmu_data_sync_barrier
mmu_data_sync_barrier:
    mov r1, #0
    mcr p15, 0, r1, c7, c10, 4          ;@ data synchronization barrier
    bx lr

.global mmu_invalidate_tlb
mmu_invalidate_tlb:
    push {lr}
    mov r1, #0
    mcr p15, 0, r1, c8, c7, 0           ;@ invalidovani zaznamu v TLB
    bl mmu_data_sync_barrier
    pop {pc}
