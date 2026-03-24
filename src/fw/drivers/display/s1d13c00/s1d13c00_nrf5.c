/*
 * Copyright 2025 Core Devices LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "s1d13c00.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "applib/graphics/gtypes.h"
#include "board/board.h"
#include "drivers/gpio.h"
#include "kernel/events.h"
#include "kernel/util/sleep.h"
#include "kernel/util/stop.h"
#include "os/mutex.h"
#include "system/passert.h"
#include <hal/nrf_gpio.h>
#include <hal/nrf_gpiote.h>
#include <hal/nrf_rtc.h>
#include <nrfx_gppi.h>
#include <nrfx_spim.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define DISP_MODE_WRITE 0x01U
#define DISP_MODE_CLEAR 0x04U

static uint8_t s_buf[5 + (PBL_DISPLAY_HEIGHT*280)];
static bool s_updating;
static bool s_updating_public;
static UpdateCompleteCallback s_uccb;
static SemaphoreHandle_t s_sem;

static inline void prv_enable_spim(void) {
  nrf_spim_enable(BOARD_CONFIG_DISPLAY.spi.p_reg);
}

static inline void prv_disable_spim(void) {
  nrf_spim_disable(BOARD_CONFIG_DISPLAY.spi.p_reg);

  // Workaround for nRF52840 anomaly 195
  // if (BOARD_CONFIG_DISPLAY.spi.p_reg == NRF_SPIM3) {
  //   *(volatile uint32_t *)0x4002F004 = 1;
  // }
}

static inline void prv_enable_chip_select(void) {
  gpio_output_set(&BOARD_CONFIG_DISPLAY.cs, false);
}

static inline void prv_disable_chip_select(void) {
  gpio_output_set(&BOARD_CONFIG_DISPLAY.cs, true);
}

void prv_iosc_enable(bool en) {
  if (!en) {
    prv_write(SYSCTRL, IOSC_TRIM+INT_POL+IOSC_20MHZ); // active low interrupt
    // no wait

  } else {
    prv_write(SYSCTRL, IOSC_TRIM+INT_POL+IOSC_20MHZ+START_IOSC); // active low interrupt
    
    // poll IOSC for stability
    uint16_t sysctrl;
    for(uint8_t i=0; i<10; i++) { // max of 5 ms
      sysctrl = prv_read(SYSCTRL);;

      if ((sysctrl & IOSC_STABLE) == IOSC_STABLE) {
        break;
      } else {
        psleep(0.5);
      }
    }
  }
}

static void prv_terminate_transfer(void *data) {
  s_updating = false;
  s_updating_public = false;

  prv_disable_chip_select();
  prv_disable_spim();

  s_uccb();
}

static void prv_spim_evt_handler(nrfx_spim_evt_t const *evt, void *ctx) {
  portBASE_TYPE woken = pdFALSE;

  prv_disable_chip_select(); // temporary...

  if (s_updating) {
    PebbleEvent e = {
        .type = PEBBLE_CALLBACK_EVENT,
        .callback =
            {
                .callback = prv_terminate_transfer,
            },
    };

    woken = event_put_isr(&e) ? pdTRUE : pdFALSE;
  } else {
    xSemaphoreGiveFromISR(s_sem, &woken);
  }

  portEND_SWITCHING_ISR(woken);
}

void display_init(void) {
  
  // ensure the voltage regulator is turned on by pulling pin 0.15 low...
  gpio_output_init(&BOARD_CONFIG_DISPLAY.on_ctrl, GPIO_OType_PP, GPIO_Speed_50MHz);
  gpio_output_set(&BOARD_CONFIG_DISPLAY.on_ctrl, false);

  nrfx_spim_config_t config = NRFX_SPIM_DEFAULT_CONFIG(
      BOARD_CONFIG_DISPLAY.clk.gpio_pin, BOARD_CONFIG_DISPLAY.mosi.gpio_pin,
      BOARD_CONFIG_DISPLAY.miso.gpio_pin, NRF_SPIM_PIN_NOT_CONNECTED);
  config.frequency = NRFX_MHZ_TO_HZ(1);
  config.bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;

  nrfx_err_t err = nrfx_spim_init(&BOARD_CONFIG_DISPLAY.spi, &config, prv_spim_evt_handler, NULL);
  PBL_ASSERTN(err == NRFX_SUCCESS);

  gpio_output_init(&BOARD_CONFIG_DISPLAY.cs, GPIO_OType_PP, GPIO_Speed_50MHz);
  gpio_output_set(&BOARD_CONFIG_DISPLAY.cs, true);

  // gpio_output_init(&BOARD_CONFIG_DISPLAY.on_ctrl, BOARD_CONFIG_DISPLAY.on_ctrl_otype,
  //                  GPIO_Speed_50MHz);
  // gpio_output_set(&BOARD_CONFIG_DISPLAY.on_ctrl, true);

  // prv_extcomin_init();

  s_sem = xSemaphoreCreateBinary();

  uint8_t stable = 0;

  // soft reset
  prv_write(SYSCTRL, RESET); 
  psleep(1); // sleep 1 ms

  // start IOSC
  prv_write(SYSCTRL, IOSC_TRIM+INT_POL+IOSC_20MHZ); // active low interrupt
  prv_write(SYSCTRL, IOSC_TRIM+INT_POL+IOSC_20MHZ+START_IOSC); // active low interrupt
  psleep(1); // sleep 1 ms
  prv_read(SYSCTRL);

  // disable FOUT pin...
  prv_write(0x40000212, 0x0); // turn GPIO in/out off
  prv_write(0x4000021c, 0x0); // turn peripheral mode off

  // enable OSC1 sequence
  prv_write(CLGINTF, OSC1STAIF); // clear int flag
  prv_write(CLGINTE, OSC1STAIE); // enable interrupt
  prv_write(SYSPROT, PROT_DIS); // disable write protect
  prv_write(CLGOSC1, OSDEN+OSC1SELCR_IN+INVIB+INVIN+OSC1WT);
  prv_write(SYSPROT, PROT_DIS); // disable write protect
  prv_write(CLGOSC, OSC1_EN); // start the OSC1
  psleep(1000); // sleep 1 s... FIXME: see if this time can be reduced later
  
  // check OSC1 stability
  prv_read(SYSINTS); // bit 6 (CLG) should be set
  prv_read(CLGINTF); // bit 1 should be set
  prv_write(CLGINTF, OSC1STAIF); // clear int flag
  prv_read(SYSINTS); // should be clear
  psleep(1); // sleep 1 ms

  // ## enable FOUT pin...
  prv_write(0x4000021e, ((0x1) << 2)); // select FOUT
  prv_write(0x4000021c, 0x2); // turn peripheral mode on
  prv_write(0x40000050, ((0x4) << 4)+((0x0) << 2)); // config FOUT
  prv_write(0x40000050, ((0x4) << 4)+((0x0) << 2)+((0x1) << 0)); // enable FOUT
  // ## measuring the internal OSC1 as 30.273 kHz... (adjust trim...)

  // power on of voltage supplies
  prv_write(MDCBSTCLK, CLKSRC_OSC1);
  prv_write(MDCBSTPWR, BSTON+REGECO+REGON);
  psleep(2); // sleep 2 ms

  // turn on VMDL (3.2 V)
  prv_write(MDCBSTVMD, VMDLVOL_3V2+VMDLON);
  psleep(2); // sleep 2 ms

  // turn on VMDH (5 V)
  prv_write(MDCBSTVMD, VMDHVOL_5V0+VMDHON+VMDLVOL_3V2+VMDLON);
  psleep(2); // sleep 2 ms

  // let display power up...
  psleep(1); // sleep 1 ms
  
  // display config (Sharp LS014B7DD01) (16 MHz clock as 20 MHz wouldn't work)
  prv_write(MDCDISPVCOMDIV, 133); // VCOM freq=32000/(4*(273+1))=~30Hz... Reflective mode
  prv_write(MDCDISPCTL, DISPGS_0+DISPSPI_0+ROTSEL_NONE+DISPEPD_0); // review + #define...
  // prv_write(MDCDISPCTL, DISPGS_0+DISPINVERT+DISPSPI_0+ROTSEL_NONE+VCOMEN+DISPEPD_0);
  prv_write(MDCDISPCTL2, 0x0);
  prv_write(MDCDISPCLKDIV, ((0) << 8)+((3) << 1)); // t0 = tsGCK2 = (6+1) T = 350 ns, T = (1)/(20 MHz) ~= 187.5 ns
  prv_write(MDCDISPPRM21, ((0) << 8)+((145) << 0)); // t2 = thsBSP = (6+1) T = 350 ns, t1 = thsGSP = 49 us
  prv_write(MDCDISPPRM43, ((35) << 8)+((0) << 0));  // t4 = [TIM4*(t3+t7) + t0 + t2] = tsGCK1 = 20 us -> TIM4 =33, t3 = tsRGB = (6+1) T = 350 ns
  prv_write(MDCDISPPRM65, ((72) << 8)+((50) << 0));  // t6 = thsINTB = 25 us, t5 = [TIM5*(t3+t7)] = thwGEN = 30 us -> TIM5 = 50
  prv_write(MDCDISPPRM87, ((0) << 8)+((0) << 0)); // t8 = thsBSP = (6+1) T = 350 ns, t7 = thRGB = (6+1) T = 350 ns
  prv_write(MDCDISPPRM109, ((2) << 8)+((2) << 0)); // t9 = 100???, t10 = 100??? FIXME
  prv_write(MDCDISPPRM1211, ((3) << 8)+((2) << 0)); // t12 = 100???, t11 = 100??? FIXME
  prv_write(MDCDISPPRM1413, ((0) << 8)+((19) << 0)); // ----, t13 = thwGCK = 1 us (Fast forward GCK)
  prv_write(MDCDISPWIDTH, 280);
  prv_write(MDCDISPHEIGHT, 280);
  prv_write(MDCDISPFRMBUFF0, RAM_START & 0xffff);
  prv_write(MDCDISPFRMBUFF1, (RAM_START >> 16) & 0xffff); // 280*280 = 78400 = 0x00013240
  prv_write(MDCDISPSTRIDE, 280); // number of bytes in each horizontal row FIXME
  prv_write(MDCDISPSTARTY, 0);
  prv_write(MDCDISPENDY, 279);

  // ## Config calculations for 20 MHz
  // prv_write(MDCDISPVCOMDIV, 266); // VCOM freq=32000/(4*(273+1))=~30Hz... Transmissive mode
  // prv_write(MDCDISPCTL, DISPGS_0+DISPINVERT+DISPSPI_0+ROTSEL_NONE+VCOMEN+DISPEPD_0); // review + #define...
  // prv_write(MDCDISPCTL2, VSTFALL+HSTFALL+ENBPHASE);
  // prv_write(MDCDISPCLKDIV, ((2) << 8)+((2) << 1)); // t0 = tsGCK2 = (6+1) T = 350 ns, T = (1)/(20 MHz) = 50 ns
  // prv_write(MDCDISPPRM21, ((2) << 8)+((167) << 0)); // t2 = thsBSP = (6+1) T = 350 ns, t1 = thsGSP = 49 us
  // prv_write(MDCDISPPRM43, ((33) << 8)+((2) << 0));  // t4 = [TIM4*(t3+t7) + t0 + t2] = tsGCK1 = 20 us -> TIM4 =33, t3 = tsRGB = (6+1) T = 350 ns
  // prv_write(MDCDISPPRM65, ((83) << 8)+((50) << 0));  // t6 = thsXRST = 25 us, t5 = [TIM5*(t3+t7)] = thwGEN = 30 us -> TIM5 = 50
  // prv_write(MDCDISPPRM87, ((2) << 8)+((2) << 0)); // t8 = thsBSP = (6+1) T = 350 ns, t7 = thRGB = (6+1) T = 350 ns
  // prv_write(MDCDISPPRM109, ((100) << 8)+((100) << 0)); // t9 = 100???, t10 = 100??? FIXME
  // prv_write(MDCDISPPRM1211, ((100) << 8)+((100) << 0)); // t12 = 100???, t11 = 100??? FIXME
  // prv_write(MDCDISPPRM1413, ((0) << 8)+((19) << 0)); // ----, t13 = thwGCK = 1 us (Fast forward GCK)
  // prv_write(MDCDISPWIDTH, PBL_DISPLAY_WIDTH);
  // prv_write(MDCDISPHEIGHT, PBL_DISPLAY_HEIGHT);
  // prv_write(MDCDISPFRMBUFF0, RAM_START & 0xffff);
  // prv_write(MDCDISPFRMBUFF1, (RAM_START >> 16) & 0xffff); // 280*280 = 78400 = 0x00013240
  // prv_write(MDCDISPSTRIDE, PBL_DISPLAY_WIDTH); // number of bytes in each horizontal row FIXME
  // prv_write(MDCDISPSTARTY, 0);
  // prv_write(MDCDISPENDY, 279);
  
  // graphics config for clearing display
  prv_write(MDCGFXOBADDR0, RAM_START & 0xffff);
  prv_write(MDCGFXOBADDR1, (RAM_START >> 16) & 0xffff); 
  prv_write(MDCGFXOWIDTH, 280);
  prv_write(MDCGFXOHEIGHT, 280);
  prv_write(MDCGFXOSTRIDE, 280);
  
  // wipe screen
  display_clear();
  prv_update_command();

  psleep(1);
  
  // enable VCOM, VB, VA
  prv_write(MDCDISPCTL, DISPGS_0+DISPSPI_0+ROTSEL_NONE+VCOMEN+DISPEPD_0);
  
  // print screen red
  prv_iosc_enable(true); // enable IOSC
  prv_display_set(0xf0);
  prv_update_command();
  prv_iosc_enable(false); // disable IOSC

  // print screen green
  prv_iosc_enable(true);
  prv_display_set(0xcc);
  prv_update_command();
  prv_iosc_enable(false);

  // print screen blue
  prv_iosc_enable(true);
  prv_display_set(0xc3);
  prv_update_command();
  prv_iosc_enable(false);

  // wipe screen
  prv_iosc_enable(true);
  display_clear();
  prv_update_command();
  prv_iosc_enable(false);

  stable = prv_read(SYSCTRL) & IOSC_STABLE;
  if(stable == 0) {
    psleep(1);
    stable = prv_read(SYSCTRL) & IOSC_STABLE;
    prv_read(SYSINTS);
  }
}

void display_clear() {

  PBL_ASSERTN(!s_updating);

  // config
  prv_write(MDCGFXIXCENTER, 0x0);
  prv_write(MDCGFXIYCENTER, 0x0);
  prv_write(MDCGFXOXCENTER, 280-1);
  prv_write(MDCGFXOYCENTER, 280-1);
  prv_write(MDCGFXCOLOR, 0xC0); // colour
  prv_write(MDCGFXIWIDTH, 0x0);
  prv_write(MDCGFXIHEIGHT, 0x0);
  prv_write(MDCGFXCTL, (1<<11));

  uint16_t intctl = prv_read(MDCINTCTL);
  prv_write(MDCINTCTL, intctl|(GFXIE+GFXIF)); // clear interrupt

  uint16_t gfxctl = prv_read(MDCGFXCTL);
  prv_write(MDCGFXCTL, gfxctl|0x2); // draw rectangle

  uint16_t trigctl = prv_read(MDCTRIGCTL);
  prv_write(MDCTRIGCTL, trigctl|(GFXTRIG));

  // wait for GXFTRIG bit to be cleared...
  // ## wait for up to 250 ms for GFXTRIG to go to 0
  for(uint8_t i=0; i<25; i++) {
    trigctl = prv_read(GFXTRIG);

    if ((trigctl && UPDTRIG) == 0) {
      break; // display update finished...
    } else {
      psleep(10);
    }
  }

  // cleanup
  intctl = prv_read(MDCINTCTL);
  prv_write(MDCINTCTL, intctl|(GFXIE+GFXIF)); // clear interrupt
}

void prv_display_set(uint8_t colour) {

  PBL_ASSERTN(!s_updating);

  // config
  prv_write(MDCGFXIXCENTER, 0x0);
  prv_write(MDCGFXIYCENTER, 0x0);
  prv_write(MDCGFXOXCENTER, 280-1);
  prv_write(MDCGFXOYCENTER, 280-1);
  prv_write(MDCGFXCOLOR, colour); // colour
  prv_write(MDCGFXIWIDTH, 0x0);
  prv_write(MDCGFXIHEIGHT, 0x0);
  prv_write(MDCGFXCTL, (1<<11));

  uint16_t intctl = prv_read(MDCINTCTL);
  prv_write(MDCINTCTL, intctl|(GFXIE+GFXIF)); // clear interrupt

  uint16_t gfxctl = prv_read(MDCGFXCTL);
  prv_write(MDCGFXCTL, gfxctl|0x2); // draw rectangle

  uint16_t trigctl = prv_read(MDCTRIGCTL);
  prv_write(MDCTRIGCTL, trigctl|(GFXTRIG));

  // wait for GXFTRIG bit to be cleared...
  // ## wait for up to 250 ms for GFXTRIG to go to 0
  for(uint8_t i=0; i<25; i++) {
    trigctl = prv_read(GFXTRIG);

    if ((trigctl && UPDTRIG) == 0) {
      break; // display update finished...
    } else {
      psleep(10);
    }
  }

  // cleanup
  intctl = prv_read(MDCINTCTL);
  prv_write(MDCINTCTL, intctl|(GFXIE+GFXIF)); // clear interrupt
}

void display_set_enabled(bool enabled) {
  // gpio_output_set(&BOARD_CONFIG_DISPLAY.on_ctrl, enabled);
}

void prv_update_command(void) {
  // ## Tell the driver IC to update the display

  uint16_t pwr = prv_read(MDCBSTPWR);
  prv_write(MDCBSTPWR, (VMDBUP+BSTON+REGON)); // speed up VMD response

  // TODO: implement a modify register func...
  uint16_t intctl = prv_read(MDCINTCTL);
  prv_write(MDCINTCTL, intctl|(UPDIE+UPDIF)); // clear interrupt

  uint16_t trig = prv_read(MDCTRIGCTL);
  prv_write(MDCTRIGCTL, trig|(UPDTRIG));

  // ## wait for up to 250 ms for trig to go to 0
  for(uint8_t i=0; i<25; i++) {
    trig = prv_read(MDCTRIGCTL);

    if ((trig & UPDTRIG) == 0) {
      break; // display update finished...
    } else {
      psleep(10);
    }
  }

  // ## cleanup
  intctl = prv_read(MDCINTCTL);
  prv_write(MDCINTCTL, intctl|(UPDIE+UPDIF)); // clear interrupt

  pwr = prv_read(MDCBSTPWR);
  prv_write(MDCBSTPWR, (BSTON+REGECO+REGON)); // turn on economy mode
}

void display_update(NextRowCallback nrcb, UpdateCompleteCallback uccb) {
  DisplayRow row;
  uint8_t *pbuf = s_buf;
  nrfx_spim_xfer_desc_t desc = {.p_tx_buffer = pbuf};

  PBL_ASSERTN(!s_updating);

  // ## Fill the memory buffer on driver IC
  *pbuf++ = PAGEPROG_CMD; // write command
  *pbuf++ = (RAM_OFFSET >> 24) & 0xff; 
  *pbuf++ = (RAM_OFFSET >> 16) & 0xff;
  *pbuf++ = (RAM_OFFSET >> 8)  & 0xff;
  *pbuf++ = RAM_OFFSET & 0xff;
  desc.tx_length += 5; // 1 command byte + 4 address bytes

  while (nrcb(&row)) {
    //pad with leading zeros (280-200)/2 = 40
    memset(pbuf, 0, 40);
    pbuf+=40;

    // write row data
    memcpy(pbuf, row.data, PBL_DISPLAY_WIDTH);
    pbuf+=PBL_DISPLAY_WIDTH;
    
    // pad with trailing zeros
    memset(pbuf, 0, 40);
    pbuf+=40;

    desc.tx_length += 280; // add length of row
  }

  // write last trailing dummy
  // *pbuf++ = 0x00;
  // desc.tx_length++;

  prv_read(SYSCTRL);
  prv_read(SYSINTS);

  s_updating_public=true;

  prv_iosc_enable(true); // enable IOSC
  prv_enable_spim();
  prv_disable_chip_select();
  prv_enable_chip_select();
  
  nrfx_err_t err = nrfx_spim_xfer(&BOARD_CONFIG_DISPLAY.spi, &desc, 0);
  PBL_ASSERTN(err == NRFX_SUCCESS);
  xSemaphoreTake(s_sem, portMAX_DELAY);

  prv_disable_chip_select();
  prv_disable_spim();

  
  prv_update_command();
  prv_iosc_enable(false); // disable IOSC

  s_uccb = uccb;
  s_updating = true;
  // dummy read to grab callback...
  // FIXME: this is disgusting
  out_buf[0]  = READ_CMD;
  out_buf[1]  = (SYSCTRL >> 24) & 0xff;
  out_buf[2]  = (SYSCTRL >> 16) & 0xff;
  out_buf[3]  = (SYSCTRL >> 8)  & 0xff;
  out_buf[4]  = SYSCTRL & 0xff;
  out_buf[5]  = 0x00; // 1 byte dummy read
  out_buf[6]  = 0x00; // 1 byte actual read
  out_buf[7]  = 0x00; // 1 byte actual read
  err = nrfx_spim_xfer(&BOARD_CONFIG_DISPLAY.spi, &(nrfx_spim_xfer_desc_t){.p_tx_buffer = &out_buf[0], .tx_length = 8,.p_rx_buffer = in_buf, .rx_length = 8}, 0);
  PBL_ASSERTN(err == NRFX_SUCCESS); // for arguments sake
}

bool display_update_in_progress(void) {
  return s_updating_public;
}

// (cmd[7:0]) (addr[31:0]) (data[7:0])...

// 16 bit addressing/registers
uint16_t prv_read(uint32_t address) {

  size_t len = 8;
  uint8_t *bob = out_buf;
  uint8_t const *out = &out_buf[0]; // making sure we are pointing to the start of the buffer...
  uint8_t *in = in_buf;

  // build the frame
  *bob++ = READ_CMD;
  *bob++ = (address >> 24) & 0xff;
  *bob++ = (address >> 16) & 0xff;
  *bob++ = (address >> 8)  & 0xff;
  *bob++ = address & 0xff;
  *bob++ = 0x00; // 1 byte dummy read
  *bob++ = 0x00; // 1 byte actual read
  *bob++ = 0x00; // 1 byte actual read
  // total read of 2 bytes (16 bit reg)

  nrfx_spim_xfer_desc_t xfer;
  xfer.p_tx_buffer = out;
  xfer.tx_length = len;
  xfer.p_rx_buffer = in;
  xfer.rx_length = len;

  prv_enable_spim();
  prv_enable_chip_select();
  
  nrfx_err_t rv = nrfx_spim_xfer(&BOARD_CONFIG_DISPLAY.spi, &xfer, 0);
  PBL_ASSERTN(rv == NRFX_SUCCESS);
  xSemaphoreTake(s_sem, portMAX_DELAY);

  prv_disable_chip_select();
  prv_disable_spim();

  return ((in[7] << 8) + (in[6]));
}


// TODO: burst write... don't have time now :(
void prv_write(uint32_t address, uint16_t value) {

  size_t len = 7;
  uint8_t *bob = out_buf;
  uint8_t const *out = &out_buf[0]; // making sure we are pointing to the start of the buffer...
  uint8_t *in = in_buf;

  // build the frame
  *bob++ = PAGEPROG_CMD;
  *bob++ = (address >> 24) & 0xff;
  *bob++ = (address >> 16) & 0xff;
  *bob++ = (address >> 8)  & 0xff;
  *bob++ = address & 0xff;
  *bob++ = value & 0xff;
  *bob++ = (value >> 8) & 0xff;

  nrfx_spim_xfer_desc_t xfer;
  xfer.p_tx_buffer = out;
  xfer.tx_length = len;
  xfer.p_rx_buffer = in;
  xfer.rx_length = len;

  prv_enable_spim();
  prv_enable_chip_select();

  nrfx_err_t rv = nrfx_spim_xfer(&BOARD_CONFIG_DISPLAY.spi, &xfer, 0);
  PBL_ASSERTN(rv == NRFX_SUCCESS);
  xSemaphoreTake(s_sem, portMAX_DELAY);
  
  prv_disable_chip_select();
  prv_disable_spim();
}

/* stubs */

uint32_t display_baud_rate_change(uint32_t new_frequency_hz) {
  return new_frequency_hz;
}

void display_pulse_vcom(void) {}

void display_show_splash_screen(void) {}

void display_set_offset(GPoint offset) {}

GPoint display_get_offset(void) {
  return GPointZero;
}
