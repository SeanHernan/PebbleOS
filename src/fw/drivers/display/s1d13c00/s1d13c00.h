/*
 * Copyright 2024 Google LLC
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

#pragma once

#include "../display.h"

#define DISP_LINE_BYTES (DISP_COLS / 8)
#define DISP_LINE_WORDS (((DISP_COLS - 1) / 32) + 1)

// Bytes_per_line + 1 byte for the line address + 1 byte for a null trailer + 1 optional byte for a write command
#define DISP_DMA_BUFFER_SIZE_BYTES (DISP_LINE_BYTES + 3)
#define DISP_DMA_BUFFER_SIZE_WORDS (DISP_LINE_WORDS + 1)

typedef enum {
  DISPLAY_STATE_IDLE,
  DISPLAY_STATE_WRITING
} DisplayState;

typedef struct {
  DisplayState state;
  NextRowCallback get_next_row;
  UpdateCompleteCallback complete;
} DisplayContext;

// s1d13c00 specific defines...
#define READ_CMD 0x03
#define FASTREAD_CMD 0x0B
#define PAGEPROG_CMD 0x02

#define RAM_START (0x20000000)
#define RAM_END (0x20017fff)
#define OFFSET (0x1c70) // calculate offset from RAM_START... 26x280 = 7280 = 0x1c70
#define RAM_OFFSET (RAM_START + OFFSET)

// System Control
#define SYSCTRL (0x400030e0)
#define RESET ((0x1) << 15)
#define IOSC_TRIM ((0x23) << 8) // original trim was 0x9s
#define INT_POL ((0x1) << 4)
#define IOSC_STABLE ((0x1) << 3) // (read only)
#define IOSC_8MHZ ((0x0) << 1)
#define IOSC_12MHZ ((0x1) << 1)
#define IOSC_16MHZ ((0x2) << 1)
#define IOSC_20MHZ ((0x3) << 1)
#define START_IOSC ((0x1) << 0)

// System Interrupts Status
#define SYSINTS (0x400030e4)

// System Protection
#define SYSPROT (0x40000000)
#define PROT_EN (0x0000)
#define PROT_DIS (0x0096)

// Oscillation Control
#define CLGOSC (0x40000042)
#define OSC1_EN ((0x1) << 1)
#define OSC1_DIS ((0x0) << 1)

// OSC1 Control
#define CLGOSC1 (0x40000046)
#define OSDRB ((0x1) << 14)
#define OSDEN ((0x1) << 13)
#define OSC1BUP ((0x1) << 12)
#define OSC1SELCR_EXT ((0x0) << 11)
#define OSC1SELCR_IN ((0x1) << 11)
#define INVIB ((0x2) << 6)
#define INVIN ((0x1) << 4)
#define OSC1WT ((0x2) << 0)

// CLG Interrupt flag
#define CLGINTF (0x4000004c)
#define OSC1STAIF ((0x1) << 1)
#define OSC1STPIF ((0x1) << 5)

// CLG Interrupt flag
#define CLGINTE (0x4000004e)
#define OSC1STAIE ((0x1) << 1)
#define OSC1STPIE ((0x1) << 5)

// OSC1 Internal Trimming
#define GCLGOSC1TRM (0x40000054)
#define OSC1_TRIM (0x0022)

// MDC Voltage Booster/Regulator clock control
#define MDCBSTCLK (0x40003080)
#define SCRATCHPAD_BIT0 ((0x1) << 7)
#define CLKDIV ((0x5) << 4)
#define CLKSRC_OSC1 ((0x1) << 0)
#define CLKSRC_IOSC ((0x0) << 0)

// MDC Power Output control
#define MDCBSTPWR (0x40003084)
#define VMDBUP ((0x1) << 3)
#define BSTON ((0x1) << 2)
#define REGECO ((0x1) << 1)
#define REGON ((0x1) << 0)

// MDC Voltage Booster/Regulator VMD Output Control
#define MDCBSTVMD (0x40003088)
#define VMDHVOL_5V0 ((0x7) << 12)
#define VMDHVOL_4V5 ((0x2) << 12)
#define VMDHON ((0x1) << 8)
#define VMDLVOL_3V2 ((0x4) << 4)
#define VMDLON ((0x1) << 0)


// MDC Display Control
#define MDCDISPCTL (0x40003000)
#define DISPGS_0 ((0x0) << 11)
#define DISPINVERT ((0x1) << 7)
#define DISPSPI_0 ((0x0) << 4)
#define ROTSEL_270 ((0x3) << 2)
#define ROTSEL_180 ((0x2) << 2)
#define ROTSEL_90 ((0x1) << 2)
#define ROTSEL_NONE ((0x0) << 2)
#define VCOMEN ((0x1) << 1)
#define DISPEPD_0 ((0x0) << 0)

// MDC Display Width
#define MDCDISPWIDTH (0x40003002)

// MDC Display height
#define MDCDISPHEIGHT (0x40003004)

// MDC VCOM clock divider
#define MDCDISPVCOMDIV (0x40003006)

// MDC Display clock divider
#define MDCDISPCLKDIV (0x40003008)

// MDC Display paramters 1 and 2
#define MDCDISPPRM21 (0x4000300a)

// MDC Display paramters 3 and 4
#define MDCDISPPRM43 (0x4000300c)

// MDC Display paramters 5 and 6
#define MDCDISPPRM65 (0x4000300e)

// MDC Display paramters 7 and 8
#define MDCDISPPRM87 (0x40003010)

// MDC Display update start line
#define MDCDISPSTARTY (0x40003012)

// MDC Display update start line
#define MDCDISPENDY (0x40003014)

// MDC Display stride
#define MDCDISPSTRIDE (0x40003016)

// MDC Display frame buffer base address 0
#define MDCDISPFRMBUFF0 (0x40003018)

// MDC Display frame buffer base address 1
#define MDCDISPFRMBUFF1 (0x4000301a)

// MDC Trigger control
#define MDCTRIGCTL (0x4000301c)
#define UPDTRIG ((0x1) << 1)
#define GFXTRIG ((0x1) << 0)

// MDC Interrupt control
#define MDCINTCTL (0x4000301e)
#define VCNTIE ((0x1) << 10)
#define UPDIE ((0x1) << 9)
#define GFXIE ((0x10) << 8)
#define VCNTIF ((0x1) << 2)
#define UPDIF ((0x1) << 1)
#define GFXIF ((0x1) << 1)

// MDC Graphics control
#define MDCGFXCTL (0x40003020)
#define FILLEN ((0x1) << 11)

// MDC Input X coordinate
#define MDCGFXIXCENTER (0x40003022)

// MDC Input Y coordinate
#define MDCGFXIYCENTER (0x40003024)

// MDC Input width
#define MDCGFXIWIDTH (0x40003026)

// MDC Input height
#define MDCGFXIHEIGHT (0x40003028)

// MDC Output X coordinate
#define MDCGFXOXCENTER (0x4000302a)

// MDC Output Y coordinate
#define MDCGFXOYCENTER (0x4000302c)

// MDC Output width
#define MDCGFXOWIDTH (0x4000302e)

// MDC Output height
#define MDCGFXOHEIGHT (0x40003030)

// MDC Input height
#define MDCGFXIHEIGHT (0x40003028)

// MDC X Left scale
#define MDCGFXXLSCALE (0x40003032)

// MDC X Right scale
#define MDCGFXXRSCALE (0x40003034)

// MDC Y Top scale
#define MDCGFXXYTSCALE (0x40003036)

// MDC Y Bottom scale
#define MDCGFXXYBSCALE (0x40003038)

// MDC X/Y Shear
#define MDCGFXSHEAR (0x4000303a)

// MDC Rotation
#define MDCGFXROTVAL (0x4000303c)

// MDC Colour
#define MDCGFXCOLOR (0x4000303e)

// MDC Source window base address 0
#define MDCGFXIBADDR0 (0x40003040)

// MDC Source window base address 1
#define MDCGFXIBADDR1 (0x40003042)

// MDC Destination window base address 0
#define MDCGFXOBADDR0 (0x40003044)

// MDC Destination window base address 1
#define MDCGFXOBADDR1 (0x40003046)

// MDC Source Image stride
#define MDCGFXISTRIDE (0x40003048)

// MDC Destination Image stride
#define MDCGFXOSTRIDE (0x4000304a)

// MDC Output window left edge
#define MDCGFXOWLEFT (0x4000304c)

// MDC Output window right edge
#define MDCGFXOWRIGHT (0x4000304e)

// MDC Output window top edge
#define MDCGFXOWTOP (0x40003050)

// MDC Output window bottom edge
#define MDCGFXOWBOT (0x40003052)

// MDC Display paramters 9 and 10
#define MDCDISPPRM109 (0x40003054)

// MDC Display paramters 11 and 12
#define MDCDISPPRM1211 (0x40003056)

// MDC Display paramters 13 and 14
#define MDCDISPPRM1413 (0x40003058)

// MDC Display control 2
#define MDCDISPCTL2 (0x4000305a)
#define GSALPHA ((0x1) << 5)
#define CSPOL ((0x1) << 4)
#define VSTFALL ((0x1) << 3)
#define HSTFALL ((0x1) << 2)
#define ENBPHASE ((0x1) << 1)
#define FASTVCK ((0x1) << 0)

// MDC VCK count compare
#define MDCVCNTCOMP (0x4000305c)

// MDC VCK count
#define MDCVCNT (0x4000305e)


// MCD VCOM Clock control register
#define MDCVCOMCLKCTL (0x40003068)

// MDC Voltage Booster/regulator VMD output control
#define MDCBSTTVMD (0x40003088)


static uint8_t out_buf[32];
static uint8_t in_buf[32];

void prv_display_set(uint8_t colour);
void prv_update_command();
uint16_t prv_read(uint32_t address);
void prv_write(uint32_t address, uint16_t value);

