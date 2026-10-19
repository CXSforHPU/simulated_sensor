/*
 * Copyright (c) 2026, CXSforHPU
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-10-19     CXSforHPU    rt_uintptr_t compat shim mirroring RT-Thread's rttypes.h branch
 */

#ifndef SIMU_SENSOR_PORT_H
#define SIMU_SENSOR_PORT_H

#include <rtthread.h>

/* Older RT-Thread headers (v3.x and some Nano forks) don't typedef
 * rt_uintptr_t; mirror rttypes.h's split so the package compiles either way. */
#if !defined(rt_uintptr_t)
    #if defined(RT_USING_LIBC) && !defined(RT_USING_NANO)
typedef uintptr_t                       rt_uintptr_t;   /**< Type for unsigned pointer length integer */
    #else
typedef rt_ubase_t                      rt_uintptr_t;   /**< Type for unsigned pointer length integer */
    #endif /* defined(RT_USING_LIBC) && !defined(RT_USING_NANO) */
#endif

#endif /* SIMU_SENSOR_PORT_H */
