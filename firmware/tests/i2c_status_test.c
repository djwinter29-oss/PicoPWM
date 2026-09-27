#include "i2c/i2c_status.h"

#include <assert.h>

int main(void) {
    volatile i2c_reg_status_t reg = {
        .epoch = 0u,
        .status = 0u,
    };
    uint32_t claimed_epoch;

    i2c_reg_status_set(&reg, 1u);
    claimed_epoch = reg.epoch;
    assert(reg.status == 1u);

    /* A newer attempt bumps the epoch. The older completion must leave BUSY in place. */
    i2c_reg_status_set(&reg, 1u);
    i2c_reg_status_complete(&reg, claimed_epoch, 0u);
    assert(reg.status == 1u);
    assert(reg.epoch != claimed_epoch);

    claimed_epoch = reg.epoch;
    i2c_reg_status_complete(&reg, claimed_epoch, 0u);
    assert(reg.status == 0u);
    assert(reg.epoch == claimed_epoch);

    return 0;
}
