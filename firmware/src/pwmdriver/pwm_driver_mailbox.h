/**
 * @file pwm_driver_mailbox.h
 * @brief Pure one-slot mailbox decisions shared by the firmware and host tests.
 */

#ifndef PWM_DRIVER_MAILBOX_H
#define PWM_DRIVER_MAILBOX_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Mailbox lifecycle states for the one-slot Core 0/Core 1 command exchange. */
typedef enum {
    PWM_DRIVER_MAILBOX_IDLE = 0, /**< No command is pending or waiting for collection. */
    PWM_DRIVER_MAILBOX_PENDING,  /**< Core 0 published a command that Core 1 has not claimed yet. */
    PWM_DRIVER_MAILBOX_ACTIVE,   /**< Core 1 claimed the command and is applying it. */
    PWM_DRIVER_MAILBOX_COMPLETE, /**< Core 1 published a reply for the last admitted command. */
} pwm_driver_mailbox_state_t;

/** @brief What Core 0 should do when the apply deadline expires. */
typedef enum {
    PWM_MAILBOX_TIMEOUT_CANCEL = 0, /**< Command is still unclaimed; drop it and return timeout. */
    PWM_MAILBOX_TIMEOUT_TAKE,       /**< Reply is already published; return that result. */
    PWM_MAILBOX_TIMEOUT_WAIT,       /**< Apply has started; keep waiting for the real result. */
} pwm_mailbox_timeout_action_t;

/** @brief Return whether Core 0 may publish a new command into this state. */
static inline bool pwm_mailbox_admits_submit(pwm_driver_mailbox_state_t state) {
    return state == PWM_DRIVER_MAILBOX_IDLE || state == PWM_DRIVER_MAILBOX_COMPLETE;
}

/**
 * @brief Choose the timeout action for the current mailbox state.
 * @param state Mailbox state observed inside the reply critical section.
 * @return Cancel, take the reply, or keep waiting.
 */
static inline pwm_mailbox_timeout_action_t pwm_mailbox_timeout_action(pwm_driver_mailbox_state_t state) {
    if (state == PWM_DRIVER_MAILBOX_PENDING) {
        return PWM_MAILBOX_TIMEOUT_CANCEL;
    }
    if (state == PWM_DRIVER_MAILBOX_COMPLETE) {
        return PWM_MAILBOX_TIMEOUT_TAKE;
    }
    return PWM_MAILBOX_TIMEOUT_WAIT;
}

#endif
