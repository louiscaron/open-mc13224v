/*
 * Generate a pattern on a GPIO
 *
 *    Copyright (C) 2013 Louis Caron
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "proc/proc.h"

#include "common/Uart1.h"

#include "reg_gpio.h"
#include "reg_crm.h"
#include "reg_itc.h"
#include "reg_tmr0.h"
#include "reg_tmr1.h"


__FIQ void FiqHandler(void)
{
    uint8_t fiq;

    // check were the FIQ is coming from
    fiq = itc_fivector_getf();

    switch (fiq)
    {
    default:
        Uart1PutS("\nUnsupported FIQ");
        ASSERT(0);
        break;
    }
}

/**
 * Set the basic configuration for the whole platform.  This can vary with the
 * application.
 */
static void
InitPlatform(void)
{
    // CRM configuration:
    // + system configuration
    //   * the clock frequency for the whole platform to 24MHz (divider = 0)
    //   * JTAG security enforced off
    //   * SPIF uses 1.8
    //   * power source is VBATT
    crm_sys_cntl_pack(0, 0, 1, 1, 0, 0);

    // + wakeup configuration
    //   + enable WU pads and interrupts
    //   + configure edge detection on low transition
    crm_wu_cntl_set((crm_wu_cntl_get() & ~EXT_WU_POL_MASK) |
            EXT_WU_IEN_MASK | EXT_WU_EN_MASK | EXT_WU_EDGE_MASK);

    // + ring oscillator configuration
    //   + tune the 2kHz oscillator (coarse=11, fine=24, dependent on every chip)
    crm_ringosc_cntl_pack(11, 24, 1);

    // + status configuration
    //   * clear any pending interrupt
    crm_status_set(0xFFFF);

    // GPIO configuration:
    // + direction configuration
    //   * configure the GPIOs 25-23 as output (KBI3-KBI1), connect to LED control
    gpio_pad_dir0_set(7 << 23);

    // + function configuration
    //   * configure the GPIO15-14 to UART1 (UART1 TX and RX)
    gpio_func_sel0_set((0x01 << (14*2)) | (0x01 << (15*2)));

    // + pull up configuration
    //   * enable the PU on the KBI[7..4] pads
    gpio_pad_pu_en0_set(gpio_pad_pu_en0_get() | (0xF<<26));
    gpio_pad_pu_sel0_set(gpio_pad_pu_sel0_get() | (0xF<<26));

    // + init data configuration
    //   * clear the LEDs
    gpio_data0_set(1<<23);

    // ITC configuration:
    // + disable all interrupts in interrupt controller
    itc_intenable_setf(0);
    // + set all interrupts to IRQ
    itc_inttype_setf(0);

    // clear pending interrupts from the CRM after the GPIO PD/PU configuration is stable
    {
        volatile uint32_t toto =0;
        while (toto++ < 10000) ;
    }
    crm_status_set(0xFFFF);

}


void Main(void)
{
    int state;
    uint16_t limit;
    uint32_t value;

    // initialize the whole platform
    InitPlatform();

    // initialize the UART1
    Uart1Init();

    // configure timer 0:
    //    - count rising edges
    //    - primary source = peripheral clock divide by 128 (0xf)
    //    - count repeatedly
    //    - count up
    //    - no co_init and no OFLAG
    tmr0_ctrl_pack(1, 0xf, 0, 0, 0, 0, 0, 0);
    tmr0_sctrl_set(0);
    tmr0_csctrl_set(0);

    state = 0;
    while (1)
    {
        // reinit the TMR0 counter
        tmr0_cntr_set(0);

        switch (state)
        {
        case 0:
            limit = 2;
            value = 1 << 23;
            state = 1;
            break;
        case 1:
            limit = 3;
            value = 0;
            // 3 pulses option
            state = 2;
            
            // 1 pulse option
            //state = 6;
            break;
        case 2:
            limit = 2;
            value = 1 << 23;
            state = 3;
            break;
        case 3:
            limit = 3;
            value = 0;
            state = 4;
            break;
        case 4:
            limit = 2;
            value = 1 << 23;
            state = 5;
            break;
        case 5:
            limit = 3;
            value = 0;
            state = 6;
            break;
        case 6:
            limit = 3125;
            value = 0;
            state = 0;
            break;
        }
        
        while (tmr0_cntr_get() < limit) ;
        
        gpio_data0_set(value);
    }
}
