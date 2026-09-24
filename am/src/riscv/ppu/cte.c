/* CTE for the riscv32-ppu platform.
 *
 * Ported from am/src/riscv/nemu but with the debug printf removed and
 * kcontext/ucontext actually implemented -- the nemu copy returns NULL for
 * both, which breaks anything that spawns a thread or enters user mode.
 *
 * The trap entry is riscv/ppu/trap.S, copied verbatim from riscv/nemu. */

#include <am.h>
#include <riscv/riscv.h>
#include <klib.h>

static Context *(*user_handler)(Event ev, Context *ctx) = NULL;

/* mcause 11 = ecall from M-mode, which is how yield() traps. */
#define ECALL_M 11

Context *__am_irq_handle(Context *c) {
  if (user_handler) {
    Event ev = {0};
    switch (c->mcause) {
      case ECALL_M:
        ev.event = EVENT_YIELD;
        c->mepc += 4;
        break;
      default:
        ev.event = EVENT_ERROR;
        break;
    }
    c = user_handler(ev, c);
    assert(c != NULL);
  }
  return c;
}

extern void __am_asm_trap(void);

bool cte_init(Context *(*handler)(Event ev, Context *ctx)) {
  asm volatile("csrw mtvec, %0" : : "r"(__am_asm_trap));
  user_handler = handler;
  return true;
}

/* Build a context that starts in _entry with a0 = arg.  trap.S pops the whole
 * Context off the stack, so this has to look exactly like a saved frame. */
Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  Context *c = (Context *)((uintptr_t)kstack.end & ~((uintptr_t)15));
  c = (Context *)((uintptr_t)c - sizeof(Context));
  *c = (Context){0};

  c->mepc   = (uintptr_t)entry;
  c->gpr[10] = (uintptr_t)arg;        /* a0 */
  c->gpr[2]  = (uintptr_t)c;          /* sp: the frame itself */
  c->mstatus = 0x1800;                /* MPP = M-mode */

  return c;
}

void yield(void) {
#ifdef __riscv_e
  asm volatile("li a5, -1; ecall");
#else
  asm volatile("li a7, -1; ecall");
#endif
}

bool ienabled(void) {
  uintptr_t mstatus;
  asm volatile("csrr %0, mstatus" : "=r"(mstatus));
  return (mstatus & (1u << 3)) != 0;  /* MSTATUS_MIE */
}

void iset(bool enable) {
  uintptr_t mstatus;
  asm volatile("csrr %0, mstatus" : "=r"(mstatus));
  if (enable) mstatus |= (1u << 3);
  else        mstatus &= ~(1u << 3);
  asm volatile("csrw mstatus, %0" : : "r"(mstatus));
}
