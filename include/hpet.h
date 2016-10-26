/*
 *  High Precision Event Timer emulation
 *
 *  Copyright (c) 2007 Alexander Graf
 *  Copyright (c) 2008 IBM Corporation
 *
 *  Authors: Beth Kon <bkon@us.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************/
 
#ifndef QEMU_HPET_EMUL_H
#define QEMU_HPET_EMUL_H

#include "typeinfo.h"/*
*  High Precision Event Timer emulation
*
*  Copyright (c) 2007 Alexander Graf
*  Copyright (c) 2008 IBM Corporation
*
*  Authors: Beth Kon <bkon@us.ibm.com>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, see <http://www.gnu.org/licenses/>.
*
* *****************************************************************/

#define HPET_BASE               0xfed00000
#define HPET_CLK_PERIOD         10000000ULL /* 10000000 femtoseconds == 10ns*/

#define FS_PER_NS 1000000
#define HPET_MIN_TIMERS         3
#define HPET_MAX_TIMERS         32

#define HPET_NUM_IRQ_ROUTES     32

#define HPET_LEGACY_PIT_INT     0
#define HPET_LEGACY_RTC_INT     1

#define HPET_CFG_ENABLE 0x001
#define HPET_CFG_LEGACY 0x002

#define HPET_ID         0x000
#define HPET_PERIOD     0x004
#define HPET_CFG        0x010
#define HPET_STATUS     0x020
#define HPET_COUNTER    0x0f0
#define HPET_TN_CFG     0x000
#define HPET_TN_CMP     0x008
#define HPET_TN_ROUTE   0x010
#define HPET_CFG_WRITE_MASK  0x3

#define HPET_ID_NUM_TIM_SHIFT   8
#define HPET_ID_NUM_TIM_MASK    0x1f00

#define HPET_TN_TYPE_LEVEL       0x002
#define HPET_TN_ENABLE           0x004
#define HPET_TN_PERIODIC         0x008
#define HPET_TN_PERIODIC_CAP     0x010
#define HPET_TN_SIZE_CAP         0x020
#define HPET_TN_SETVAL           0x040
#define HPET_TN_32BIT            0x100
#define HPET_TN_INT_ROUTE_MASK  0x3e00
#define HPET_TN_FSB_ENABLE      0x4000
#define HPET_TN_FSB_CAP         0x8000
#define HPET_TN_CFG_WRITE_MASK  0x7f4e
#define HPET_TN_INT_ROUTE_SHIFT      9
#define HPET_TN_INT_ROUTE_CAP_SHIFT 32
#define HPET_TN_CFG_BITS_READONLY_OR_RESERVED 0xffff80b1U

struct hpet_fw_entry
{
    uint32_t event_timer_block_id;
    uint64_t address;
    uint16_t min_tick;
    uint8_t page_prot;
} QEMU_PACKED;

struct hpet_fw_config
{
    uint8_t count;
    struct hpet_fw_entry hpet[8];
} QEMU_PACKED;

extern struct hpet_fw_config hpet_cfg;

#define TYPE_HPET "hpet"

static inline bool hpet_find(void)
{
    return NULL;
}

#endif
