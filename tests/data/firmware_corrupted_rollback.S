/* Deliberately broken Cortex-M3 application for failed-boot rollback testing.
 * Execute UDF immediately, then reset from HardFault without confirming boot.
 * No UART, flash/NV writes, watchdog, HAL or working application code is used.
 * All image-relative addresses resolve at assembly time; no linker is needed.
 * See doc/firmware-build.md for reproducible packaging commands.
 */
.syntax unified
.cpu cortex-m3
.thumb
.section .text,"ax",%progbits
.balign 4
image_start:
    .word 0x20005000
    .word 0x08005200 + (reset_handler - image_start) + 1
    .rept 14
    .word 0x08005200 + (fault_handler - image_start) + 1
    .endr

reset_handler:
    udf #0
    b .

fault_handler:
    cpsid i
    ldr r0, =0xe000ed0c       /* SCB AIRCR */
    ldr r1, =0x05fa0004       /* VECTKEY | SYSRESETREQ */
    dsb
    str r1, [r0]
    dsb
    b .
    .ltorg
    /* Keep a complete vector/code payload inside the checksummed region. */
    .balign 512, 0xff
