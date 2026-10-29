#include "../sdk_stub.h"

#include <assert.h>
#include <stdatomic.h>

enum { IO_IRQ_BANK0 = 13u };

extern atomic_bool test_monitor_irq_enabled;

static inline void irq_set_enabled(uint irq, bool enabled) {
    assert(get_core_num() == 1u);
    assert(irq == IO_IRQ_BANK0);
    atomic_store(&test_monitor_irq_enabled, enabled);
}
