/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Ha Thach for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "shared-bindings/microcontroller/__init__.h"
#include "shared-bindings/busio/UART.h"

#include "lib/utils/interrupt_char.h"
#include "py/mpconfig.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "supervisor/shared/translate.h"

#include "tick.h"
#include "nrfx_uarte.h"
#include <string.h>

#ifdef NRF52840_XXAA

// expression to examine, and return value in case of failing
#define _VERIFY_ERR(_exp) \
    do {\
      uint32_t _err = (_exp);\
      if (NRFX_SUCCESS != _err ) {\
        mp_raise_msg_varg(&mp_type_RuntimeError, translate("error = 0x%08lX"), _err);\
      }\
    }while(0)

static uint32_t get_nrf_baud (uint32_t baudrate);

static uint16_t ringbuf_count(ringbuf_t *r)
{
    volatile int count = r->iput - r->iget;
    if ( count < 0 ) {
        count += r->size;
    }

    return (uint16_t) count;
}

static void ringbuf_clear(ringbuf_t *r)
{
    r->iput = r->iget = 0;
}

static void uart_callback_irq (const nrfx_uarte_event_t * event, void * context) {
    busio_uart_obj_t* self = (busio_uart_obj_t*) context;

    switch ( event->type ) {
        case NRFX_UARTE_EVT_RX_DONE:
            for(uint8_t i=0; i < event->data.rxtx.bytes; i++) {
                ringbuf_put(&self->rbuf, event->data.rxtx.p_data[i]);
            }

            // keep receiving
            _VERIFY_ERR(nrfx_uarte_rx(&self->uarte, &self->rx_char, 1));
        break;

        case NRFX_UARTE_EVT_TX_DONE:
            // nothing to do
        break;

        case NRFX_UARTE_EVT_ERROR:
            // Handle error
        break;

        default:
        break;
    }
}


void common_hal_busio_uart_construct (busio_uart_obj_t *self,
                                      const mcu_pin_obj_t * tx, const mcu_pin_obj_t * rx, uint32_t baudrate,
                                      uint8_t bits, uart_parity_t parity, uint8_t stop, mp_float_t timeout,
                                      uint8_t receiver_buffer_size) {
    if ( (tx == mp_const_none) && (rx == mp_const_none) ) {
        mp_raise_ValueError(translate("tx and rx cannot both be None"));
    }

    if ( receiver_buffer_size == 0 ) {
        mp_raise_ValueError(translate("Invalid buffer size"));
    }

    if ( parity == PARITY_ODD ) {
        mp_raise_ValueError(translate("Odd parity is not supported"));
    }

    nrfx_uarte_config_t config = {
        .pseltxd = (tx == mp_const_none) ? NRF_UARTE_PSEL_DISCONNECTED : tx->number,
        .pselrxd = (rx == mp_const_none) ? NRF_UARTE_PSEL_DISCONNECTED : rx->number,
        .pselcts = NRF_UARTE_PSEL_DISCONNECTED,
        .pselrts = NRF_UARTE_PSEL_DISCONNECTED,
        .p_context = self,
        .hwfc = NRF_UARTE_HWFC_DISABLED,
        .parity = (parity == PARITY_NONE) ? NRF_UARTE_PARITY_EXCLUDED : NRF_UARTE_PARITY_INCLUDED,
        .baudrate = get_nrf_baud(baudrate),
        .interrupt_priority = 7
    };

    // support only 1 instance for now
    self->uarte = (nrfx_uarte_t ) NRFX_UARTE_INSTANCE(0);
    nrfx_uarte_uninit(&self->uarte);
    _VERIFY_ERR(nrfx_uarte_init(&self->uarte, &config, uart_callback_irq));

    // Init buffer for rx
    if ( rx != mp_const_none ) {
        self->rbuf.buf = (uint8_t *) gc_alloc(receiver_buffer_size, false, false);

        if ( !self->rbuf.buf ) {
            nrfx_uarte_uninit(&self->uarte);
            mp_raise_msg(&mp_type_MemoryError, translate("Failed to allocate RX buffer"));
        }

        self->rbuf.size = receiver_buffer_size;
        self->rbuf.iget = self->rbuf.iput = 0;

        self->rx_pin_number = rx->number;
        claim_pin(rx);
    }

    if ( tx != mp_const_none ) {
        self->tx_pin_number = tx->number;
        claim_pin(tx);
    } else {
        self->tx_pin_number = NO_PIN;
    }

    self->baudrate = baudrate;
    self->timeout_ms = timeout * 1000;

    // Initial wait for incoming byte
    _VERIFY_ERR(nrfx_uarte_rx(&self->uarte, &self->rx_char, 1));
}

bool common_hal_busio_uart_deinited(busio_uart_obj_t *self) {
    return self->rx_pin_number == NO_PIN;
}

void common_hal_busio_uart_deinit(busio_uart_obj_t *self) {
    if ( !common_hal_busio_uart_deinited(self) ) {
        nrfx_uarte_uninit(&self->uarte);
        reset_pin_number(self->tx_pin_number);
        reset_pin_number(self->rx_pin_number);
        self->tx_pin_number = NO_PIN;
        self->rx_pin_number = NO_PIN;

        gc_free(self->rbuf.buf);
        self->rbuf.size = 0;
        self->rbuf.iput = self->rbuf.iget = 0;
    }
}

// Read characters.
size_t common_hal_busio_uart_read(busio_uart_obj_t *self, uint8_t *data, size_t len, int *errcode) {
    if ( nrf_uarte_rx_pin_get(self->uarte.p_reg) == NRF_UARTE_PSEL_DISCONNECTED ) {
        mp_raise_ValueError(translate("No RX pin"));
    }

    size_t rx_bytes = 0;
    uint64_t start_ticks = ticks_ms;

    // Wait for all bytes received or timeout
    while ( (ringbuf_count(&self->rbuf) < len) && (ticks_ms - start_ticks < self->timeout_ms) ) {
#ifdef MICROPY_VM_HOOK_LOOP
        MICROPY_VM_HOOK_LOOP ;
        // Allow user to break out of a timeout with a KeyboardInterrupt.
        if ( mp_hal_is_interrupted() ) {
            return 0;
        }
#endif
    }

    // prevent conflict with uart irq
    NVIC_DisableIRQ(nrfx_get_irq_number(self->uarte.p_reg));

    // copy received data
    rx_bytes = ringbuf_count(&self->rbuf);
    rx_bytes = MIN(rx_bytes, len);
    for ( uint16_t i = 0; i < rx_bytes; i++ ) {
        data[i] = ringbuf_get(&self->rbuf);
    }

    NVIC_EnableIRQ(nrfx_get_irq_number(self->uarte.p_reg));

    return rx_bytes;
}

// Write characters.
size_t common_hal_busio_uart_write (busio_uart_obj_t *self, const uint8_t *data, size_t len, int *errcode) {
    if ( nrf_uarte_tx_pin_get(self->uarte.p_reg) == NRF_UARTE_PSEL_DISCONNECTED ) {
        mp_raise_ValueError(translate("No TX pin"));
    }

    if ( len == 0 ) return 0;

    uint64_t start_ticks = ticks_ms;

    // Wait for on-going transfer to complete
    while ( nrfx_uarte_tx_in_progress(&self->uarte) && (ticks_ms - start_ticks < self->timeout_ms) ) {
#ifdef MICROPY_VM_HOOK_LOOP
        MICROPY_VM_HOOK_LOOP
#endif
    }

    // Time up
    if ( !(ticks_ms - start_ticks < self->timeout_ms) ) {
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }

    // EasyDMA can only access SRAM
    uint8_t * tx_buf = (uint8_t*) data;
    if ( !nrfx_is_in_ram(data) ) {
        tx_buf = (uint8_t *) gc_alloc(len, false, false);
        memcpy(tx_buf, data, len);
    }

    (*errcode) = nrfx_uarte_tx(&self->uarte, tx_buf, len);
    _VERIFY_ERR(*errcode);
    (*errcode) = 0;

    while ( nrfx_uarte_tx_in_progress(&self->uarte) && (ticks_ms - start_ticks < self->timeout_ms) ) {
#ifdef MICROPY_VM_HOOK_LOOP
        MICROPY_VM_HOOK_LOOP
#endif
    }

    if ( !nrfx_is_in_ram(data) ) {
        gc_free(tx_buf);
    }

    return len;
}

uint32_t common_hal_busio_uart_get_baudrate(busio_uart_obj_t *self) {
    return self->baudrate;
}

void common_hal_busio_uart_set_baudrate(busio_uart_obj_t *self, uint32_t baudrate) {
    self->baudrate = baudrate;
    nrf_uarte_baudrate_set(self->uarte.p_reg, get_nrf_baud(baudrate));
}

uint32_t common_hal_busio_uart_rx_characters_available(busio_uart_obj_t *self) {
    return ringbuf_count(&self->rbuf);
}

void common_hal_busio_uart_clear_rx_buffer(busio_uart_obj_t *self) {
    // prevent conflict with uart irq
    NVIC_DisableIRQ(nrfx_get_irq_number(self->uarte.p_reg));
    ringbuf_clear(&self->rbuf);
    NVIC_EnableIRQ(nrfx_get_irq_number(self->uarte.p_reg));
}

bool common_hal_busio_uart_ready_to_tx(busio_uart_obj_t *self) {
    return !nrfx_uarte_tx_in_progress(&self->uarte);
}

static uint32_t get_nrf_baud (uint32_t baudrate)
{
    if ( baudrate <= 1200 ) {
        return NRF_UARTE_BAUDRATE_1200;
    }
    else if ( baudrate <= 2400 ) {
        return NRF_UARTE_BAUDRATE_2400;
    }
    else if ( baudrate <= 4800 ) {
        return NRF_UARTE_BAUDRATE_4800;
    }
    else if ( baudrate <= 9600 ) {
        return NRF_UARTE_BAUDRATE_9600;
    }
    else if ( baudrate <= 14400 ) {
        return NRF_UARTE_BAUDRATE_14400;
    }
    else if ( baudrate <= 19200 ) {
        return NRF_UARTE_BAUDRATE_19200;
    }
    else if ( baudrate <= 28800 ) {
        return NRF_UARTE_BAUDRATE_28800;
    }
    else if ( baudrate <= 38400 ) {
        return NRF_UARTE_BAUDRATE_38400;
    }
    else if ( baudrate <= 57600 ) {
        return NRF_UARTE_BAUDRATE_57600;
    }
    else if ( baudrate <= 76800 ) {
        return NRF_UARTE_BAUDRATE_76800;
    }
    else if ( baudrate <= 115200 ) {
        return NRF_UARTE_BAUDRATE_115200;
    }
    else if ( baudrate <= 230400 ) {
        return NRF_UARTE_BAUDRATE_230400;
    }
    else if ( baudrate <= 250000 ) {
        return NRF_UARTE_BAUDRATE_250000;
    }
    else if ( baudrate <= 460800 ) {
        return NRF_UARTE_BAUDRATE_460800;
    }
    else if ( baudrate <= 921600 ) {
        return NRF_UARTE_BAUDRATE_921600;
    }
    else {
        return NRF_UARTE_BAUDRATE_1000000;
    }
}

#else

void common_hal_busio_uart_construct (busio_uart_obj_t *self,
                                      const mcu_pin_obj_t * tx, const mcu_pin_obj_t * rx, uint32_t baudrate,
                                      uint8_t bits, uart_parity_t parity, uint8_t stop, float timeout,
                                      uint8_t receiver_buffer_size) {
    mp_raise_NotImplementedError(translate("busio.UART not available"));
}

bool common_hal_busio_uart_deinited (busio_uart_obj_t *self) {
    mp_raise_NotImplementedError(translate("busio.UART not available"));
    return true;
}

void common_hal_busio_uart_deinit (busio_uart_obj_t *self) {
    mp_raise_NotImplementedError(translate("busio.UART not available"));
}

// Read characters.
size_t common_hal_busio_uart_read (busio_uart_obj_t *self, uint8_t *data, size_t len, int *errcode) {
    mp_raise_NotImplementedError(translate("busio.UART not available"));
    return 0;
}

// Write characters.
size_t common_hal_busio_uart_write (busio_uart_obj_t *self, const uint8_t *data, size_t len, int *errcode) {
    mp_raise_NotImplementedError(translate("busio.UART not available"));
    return 0;
}

uint32_t common_hal_busio_uart_get_baudrate (busio_uart_obj_t *self) {
    mp_raise_NotImplementedError(translate("busio.UART not available"));
    return self->baudrate;
}

void common_hal_busio_uart_set_baudrate (busio_uart_obj_t *self, uint32_t baudrate) {
    mp_raise_NotImplementedError(translate("busio.UART not available"));
}

uint32_t common_hal_busio_uart_rx_characters_available (busio_uart_obj_t *self) {
    mp_raise_NotImplementedError(translate("busio.UART not available"));
}

void common_hal_busio_uart_clear_rx_buffer (busio_uart_obj_t *self) {

}

bool common_hal_busio_uart_ready_to_tx (busio_uart_obj_t *self) {
    mp_raise_NotImplementedError(translate("busio.UART not available"));
    return false;
}
#endif
