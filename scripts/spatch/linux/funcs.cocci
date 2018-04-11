@@
expression T;
@@
-msleep(T);
+kthread_usleep(1000 * T);

@@
expression TMIN;
expression TMAX;
@@
-usleep_range(TMIN, TMAX);
+kthread_usleep(TMIN);

// barriers
@@
@@
-barrier();
+cmb();

@@
@@
-smp_mb();
+mb();

@@
@@
-smp_rmb();
+rmb();

@@
@@
-smp_wmb();
+wmb();

@@
expression A0;
expression A1;
@@
-min(A0, A1)
+MIN(A0, A1)

@@
expression A0;
expression A1;
@@
-max(A0, A1)
+MAX(A0, A1)

@@
expression LO;
expression HI;
expression V;
@@
-clamp(V, LO, HI)
+CLAMP(V, LO, HI)

@@
expression A0;
expression A1;
type T;
@@
-min_t(T, A0, A1)
+MIN_T(T, A0, A1)

@@
expression A0;
expression A1;
type T;
@@
-max_t(T, A0, A1)
+MAX_T(T, A0, A1)

@@
expression LO;
expression HI;
expression V;
type T;
@@
-clamp_t(T, V, LO, HI)
+CLAMP_T(T, V, LO, HI)


// locking
// being conservative: they might not need irqsave
@@
expression E;
@@
-spin_lock_init(E)
+spinlock_init_irqsave(E)

@@
expression E;
@@
-spin_lock_bh(E)
+spin_lock(E)

@@
expression E;
@@
-spin_unlock_bh(E)
+spin_unlock(E)
@@

expression E;
@@
-spin_lock_irq(E)
+spin_lock_irqsave(E)

@@
expression E;
@@
-spin_unlock_irq(E)
+spin_unlock_irqsave(E)

@@
expression lock, flags;
@@
-spin_lock_irqsave(lock, flags)
+spin_lock_irqsave(lock)
...
-spin_unlock_irqrestore(lock, flags)
+spin_unlock_irqsave(lock)

@@
expression E;
@@
-ilog2(E)
+LOG2_UP(E)

@@
expression E;
@@
-roundup_pow_of_two(E)
+ROUNDUPPWR2(E)

@@
expression E;
@@
-rounddown_pow_of_two(E)
+ROUNDDOWNPWR2(E)

@@
expression E;
@@
-is_power_of_2(E)
+IS_PWR2(E)

@@
expression DST;
expression SRC;
expression LEN;
@@
-copy_from_user(DST, SRC, LEN)
+memcpy_from_user(current, DST, SRC, LEN)

@@
expression DST;
expression SRC;
expression LEN;
@@
-copy_to_user(DST, SRC, LEN)
+memcpy_to_user(current, DST, SRC, LEN)

@@
@@
-ktime_get_real()
+epoch_nsec()

@@
expression E;
@@
-ktime_to_ns(E)
+E

@@
expression E;
@@
-htonl(E)
+cpu_to_be32(E)

@@
expression E;
@@
-htons(E)
+cpu_to_be16(E)

@@
expression E;
@@
-ntohl(E)
+be32_to_cpu(E)

@@
expression E;
@@
-ntohs(E)
+be16_to_cpu(E)

@@
@@
-smp_processor_id()
+core_id()

// This is a little half-assed.  Any fix will need to be manually edited to
// provide a pointer to the pci device.  And you'll need to fix your handler to
// be the correct type.
@@
expression IRQ;
expression HANDLER;
expression FLAGS;
expression NAME;
expression ARG;
@@
-request_irq(IRQ, HANDLER, FLAGS, NAME, ARG)
+register_irq(IRQ, HANDLER, ARG, pci_to_tbdf(PCIDEV))

// There are 3 return types for the irq handlers, IRQ_NONE, IRQ_HANDLED, and
// IRQ_WAKE_THREAD.  We can change the first two to just return.  The latter
// will need manual attention, since they want a thread to handle the rest.
@@
identifier HANDLER;
typedef irqreturn_t;
@@
irqreturn_t HANDLER(...) {
<...
-return IRQ_NONE;
+return;
...>
}

// Need to comment out irqreturn_t, I guess because it's in a previous rule
@@
identifier HANDLER;
//typedef irqreturn_t;
@@
irqreturn_t HANDLER(...) {
<...
-return IRQ_HANDLED;
+return;
...>
}

// There should be a way to catch both decl and def at once...
// Changes the definition
@@
identifier HANDLER;
//typedef irqreturn_t;
identifier IRQ;
identifier ARG;
@@
-irqreturn_t HANDLER(int IRQ, void *ARG
+void HANDLER(struct hw_trapframe *hw_tf, void *ARG
 ) { ... }

// Changes the declaration
@@
identifier HANDLER;
//typedef irqreturn_t;
identifier IRQ;
identifier ARG;
@@
-irqreturn_t HANDLER(int IRQ, void *ARG
+void HANDLER(struct hw_trapframe *hw_tf, void *ARG
 );

@@
expression VAL;
expression UP;
@@
-roundup(VAL, UP)
+ROUNDUP(VAL, UP)

@@
expression VAL;
expression DOWN;
@@
-rounddown(VAL, DOWN)
+ROUNDDOWN(VAL, DOWN)

@@
expression STMT;
@@
-BUG_ON(STMT)
+assert(!(STMT))

@@
expression STMT;
@@
-BUILD_BUG_ON(STMT)
+static_assert(!(STMT))

@@
@@
-BUG()
+panic("BUG")

@@
@@
-WARN_ON
+warn_on

@@
@@
-WARN_ON_ONCE
+warn_on_once

@@
expression P;
@@
-pci_set_master(P)
+pci_set_bus_master(P)

@@
expression P;
@@
-pci_clear_master(P)
+pci_clr_bus_master(P)
