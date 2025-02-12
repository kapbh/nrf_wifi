/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file hal_reg.h
 *
 * @brief Header containing register read/write specific declarations for the
 * HAL Layer of the Wi-Fi driver.
 */

#ifndef __HAL_REG_H__
#define __HAL_REG_H__

#include "hal_structs_common.h"

/**
 * @brief Read from an RPU register.
 *
 * @param hal_ctx Pointer to HAL context.
 * @param val Pointer to the host memory where the value read from the RPU register
 *            is to be copied.
 * @param rpu_reg_addr Absolute value of RPU register address from which the
 *                     value is to be read.
 *
 * This function reads a 4 byte value from a RPU register and copies it
 * to the host memory pointed to by the val parameter.
 *
 * @return Status
 *         - Pass: NRF_WIFI_STATUS_SUCCESS
 *         - Error: NRF_WIFI_STATUS_FAIL
 */
enum nrf_wifi_status hal_rpu_reg_read(struct nrf_wifi_hal_dev_ctx *hal_ctx,
		unsigned int *val,
		unsigned int rpu_reg_addr);

/**
 * @brief Wnrite to an RPU register.
 *
 * @param hal_ctx Pointer to HAL context.
 * @param rpu_reg_addr Absolute value of RPU register address to which the
 *                     value is to be written.
 * @param val The value which is to be written to the RPU register.
 *
 * This function writes a 4 byte value to a RPU register.
 *
 * @return Status
 *         - Pass: NRF_WIFI_STATUS_SUCCESS
 *         - Error: NRF_WIFI_STATUS_FAIL
 */
enum nrf_wifi_status hal_rpu_reg_write(struct nrf_wifi_hal_dev_ctx *hal_ctx,
		unsigned int rpu_reg_addr,
		unsigned int val);
#endif /* __HAL_REG_H__ */
