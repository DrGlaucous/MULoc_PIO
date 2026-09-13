/*! ----------------------------------------------------------------------------
 * @file	dwm1000_timestamp.h
 * @brief	HW specific definitions and functions for portability
 *
 * @attention
 *
 * Copyright 2013 (c) DecaWave Ltd, Dublin, Ireland.
 *
 * All rights reserved.
 *
 * @author DecaWave
 */
#pragma once
#include <Arduino.h>
#include "dw3000.h"



#ifndef uint64
typedef unsigned long long uint64;
#endif

#ifndef int64
typedef signed long long int64;
#endif


#ifndef MAX_MESSAGE
#define MAX_MESSAGE 2
struct time_timestamp
{
    uint32_t tx_ts[MAX_MESSAGE];
    uint32_t rx_ts[MAX_MESSAGE];
    uint16_t shortaddr;
    bool present;
    bool timestamp_validate;
    int ts_index;
};
#endif

#ifndef MAX_TARGET_NODE
#define MAX_TARGET_NODE 5
#endif

#ifndef FIRST_TX
#define FIRST_TX 2
#define FIRST_RX 6
#define STATISTIC 10
#endif

