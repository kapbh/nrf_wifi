/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @brief File containing patch loader specific definitions for the
 * HAL Layer of the Wi-Fi driver.
 */

#include "host_rpu_common_if.h"
#include "common/hal_fw_patch_loader.h"
#include "common/hal_mem.h"
#include "lmac_if_common.h"
#include "host_rpu_common_if.h"

/* To reduce HEAP maximum usage */
#define MAX_PATCH_CHUNK_SIZE 8192
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif /* ARRAY_SIZE */

const struct nrf70_fw_addr_info nrf70_fw_addr_info[] = {
	{ RPU_PROC_TYPE_MCU_LMAC, "LMAC bimg", RPU_MEM_LMAC_PATCH_BIMG },
	{ RPU_PROC_TYPE_MCU_LMAC, "LMAC bin", RPU_MEM_LMAC_PATCH_BIN },
	{ RPU_PROC_TYPE_MCU_UMAC, "UMAC bimg", RPU_MEM_UMAC_PATCH_BIMG },
	{ RPU_PROC_TYPE_MCU_UMAC, "UMAC bin", RPU_MEM_UMAC_PATCH_BIN },
};

struct patch_contents {
	const char *id_str;
	const void *data;
	unsigned int size;
	unsigned int dest_addr;
};


static const struct rpu_mcu_boot_vectors RPU_MCU_BOOT_VECTORS[] = {
	/* MCU1 - LMAC */
	{
		{
			{RPU_REG_MIPS_MCU_BOOT_EXCP_INSTR_0, NRF_WIFI_LMAC_BOOT_EXCP_VECT_0},
			{RPU_REG_MIPS_MCU_BOOT_EXCP_INSTR_1, NRF_WIFI_LMAC_BOOT_EXCP_VECT_1},
			{RPU_REG_MIPS_MCU_BOOT_EXCP_INSTR_2, NRF_WIFI_LMAC_BOOT_EXCP_VECT_2},
			{RPU_REG_MIPS_MCU_BOOT_EXCP_INSTR_3, NRF_WIFI_LMAC_BOOT_EXCP_VECT_3},
		}
	},
	/* MCU2 - UMAC */
	{
		{
			{RPU_REG_MIPS_MCU2_BOOT_EXCP_INSTR_0, NRF_WIFI_UMAC_BOOT_EXCP_VECT_0},
			{RPU_REG_MIPS_MCU2_BOOT_EXCP_INSTR_1, NRF_WIFI_UMAC_BOOT_EXCP_VECT_1},
			{RPU_REG_MIPS_MCU2_BOOT_EXCP_INSTR_2, NRF_WIFI_UMAC_BOOT_EXCP_VECT_2},
			{RPU_REG_MIPS_MCU2_BOOT_EXCP_INSTR_3, NRF_WIFI_UMAC_BOOT_EXCP_VECT_3},
		}
	},
};


enum nrf_wifi_status hal_fw_patch_chunk_load(struct nrf_wifi_hal_dev_ctx *hal_dev_ctx,
						enum RPU_PROC_TYPE rpu_proc,
						unsigned int dest_addr,
						const void *fw_chunk_data,
						unsigned int fw_chunk_size)
{
	enum nrf_wifi_status status = NRF_WIFI_STATUS_FAIL;

	hal_dev_ctx->curr_proc = rpu_proc;

	status = hal_rpu_mem_write(hal_dev_ctx,
				 dest_addr,
				 (void *)fw_chunk_data,
				 fw_chunk_size);

	hal_dev_ctx->curr_proc = RPU_PROC_TYPE_MCU_LMAC;

	return status;
}

/* In order to save RAM, divide the patch in to chunks download */
static enum nrf_wifi_status hal_fw_patch_load(struct nrf_wifi_hal_dev_ctx *hal_dev_ctx,
						enum RPU_PROC_TYPE rpu_proc,
						const char *patch_id_str,
						unsigned int dest_addr,
						const void *fw_patch_data,
						unsigned int fw_patch_size)
{
	enum nrf_wifi_status status = NRF_WIFI_STATUS_FAIL;
	int last_chunk_size = fw_patch_size % MAX_PATCH_CHUNK_SIZE;
	int num_chunks = fw_patch_size / MAX_PATCH_CHUNK_SIZE +
					(last_chunk_size ? 1 : 0);
	int chunk = 0;

	for (chunk = 0; chunk < num_chunks; chunk++) {
		unsigned char *patch_data_ram;
		unsigned int patch_chunk_size =
			((chunk == num_chunks - 1) ? last_chunk_size : MAX_PATCH_CHUNK_SIZE);
		const void *src_patch_offset = (const char *)fw_patch_data +
			chunk * MAX_PATCH_CHUNK_SIZE;
		int dest_chunk_offset = dest_addr + chunk * MAX_PATCH_CHUNK_SIZE;

		patch_data_ram = nrf_wifi_osal_mem_alloc(patch_chunk_size);
		if (!patch_data_ram) {
			nrf_wifi_osal_log_err("%s: Mem alloc failed for patch "
					      "%s-%s: chunk %d/%d, size: %d",
					      __func__,
					      rpu_proc_to_str(rpu_proc),
					      patch_id_str,
					      chunk + 1,
					      num_chunks,
					      patch_chunk_size);
			status = NRF_WIFI_STATUS_FAIL;
			goto out;
		}

		nrf_wifi_osal_mem_cpy(patch_data_ram,
				      src_patch_offset,
				      patch_chunk_size);


		nrf_wifi_osal_log_dbg("%s: Copying patch %s-%s: chunk %d/%d, size: %d",
				      __func__,
				      rpu_proc_to_str(rpu_proc),
				      patch_id_str,
				      chunk + 1,
				      num_chunks,
				      patch_chunk_size);

		status = hal_fw_patch_chunk_load(hal_dev_ctx,
						rpu_proc,
						dest_chunk_offset,
						patch_data_ram,
						patch_chunk_size);
		if (status != NRF_WIFI_STATUS_SUCCESS) {
			nrf_wifi_osal_log_err("%s: Patch copy %s-%s: chunk %d/%d, size: %d failed",
					      __func__,
					      rpu_proc_to_str(rpu_proc),
					      patch_id_str,
					      chunk + 1,
					      num_chunks,
					      patch_chunk_size);
			goto out;
		}
out:
		if (patch_data_ram)
			nrf_wifi_osal_mem_free(patch_data_ram);
		if (status != NRF_WIFI_STATUS_SUCCESS)
			break;
	}

	return status;
}

/*
 * Copies the firmware patches to the RPU memory.
 */
enum nrf_wifi_status nrf_wifi_hal_fw_patch_load(struct nrf_wifi_hal_dev_ctx *hal_dev_ctx,
						enum RPU_PROC_TYPE rpu_proc,
						const void *fw_pri_patch_data,
						unsigned int fw_pri_patch_size,
						const void *fw_sec_patch_data,
						unsigned int fw_sec_patch_size)
{
	enum nrf_wifi_status status = NRF_WIFI_STATUS_FAIL;
	unsigned int pri_dest_addr = 0;
	unsigned int sec_dest_addr = 0;
	int patch = 0;

	if (!fw_pri_patch_data) {
		nrf_wifi_osal_log_err("%s: Primary patch missing for RPU (%d)",
				      __func__,
				      rpu_proc);
		status = NRF_WIFI_STATUS_FAIL;
		goto out;
	}

	if (!fw_sec_patch_data) {
		nrf_wifi_osal_log_err("%s: Secondary patch missing for RPU (%d)",
				      __func__,
				      rpu_proc);
		status = NRF_WIFI_STATUS_FAIL;
		goto out;
	}

	/* Set the HAL RPU context to the current required context */
	hal_dev_ctx->curr_proc = rpu_proc;

	switch (rpu_proc) {
	case RPU_PROC_TYPE_MCU_LMAC:
		pri_dest_addr = RPU_MEM_LMAC_PATCH_BIMG;
		sec_dest_addr = RPU_MEM_LMAC_PATCH_BIN;
		break;
	case RPU_PROC_TYPE_MCU_UMAC:
		pri_dest_addr = RPU_MEM_UMAC_PATCH_BIMG;
		sec_dest_addr = RPU_MEM_UMAC_PATCH_BIN;
		break;
	default:
		nrf_wifi_osal_log_err("%s: Invalid RPU processor type[%d]",
				      __func__,
				      rpu_proc);

		goto out;
	}

	/* This extra block is needed to avoid compilation error for inline
	 * declaration but still keep using const data.
	 */
	{
		const struct patch_contents patches[] = {
			{ "bimg", fw_pri_patch_data, fw_pri_patch_size, pri_dest_addr },
			{ "bin", fw_sec_patch_data, fw_sec_patch_size, sec_dest_addr },
		};

		for (patch = 0; patch < ARRAY_SIZE(patches); patch++) {
			status = hal_fw_patch_load(hal_dev_ctx,
						rpu_proc,
						patches[patch].id_str,
						patches[patch].dest_addr,
						patches[patch].data,
						patches[patch].size);
			if (status != NRF_WIFI_STATUS_SUCCESS)
				goto out;
		}
	}
out:
	/* Reset the HAL RPU context to the LMAC context */
	hal_dev_ctx->curr_proc = RPU_PROC_TYPE_MCU_LMAC;

	return status;
}


enum nrf_wifi_status nrf_wifi_hal_fw_patch_boot(struct nrf_wifi_hal_dev_ctx *hal_dev_ctx,
						enum RPU_PROC_TYPE rpu_proc,
						bool is_patch_present)
{
	enum nrf_wifi_status status = NRF_WIFI_STATUS_FAIL;
	unsigned int boot_sig_addr = 0;
	unsigned int boot_sig_val = 0;
	unsigned int boot_vector_id;
	unsigned int sleepctrl_addr = 0;
	unsigned int sleepctrl_val = 0;
	unsigned int run_addr = 0;
	const struct rpu_mcu_boot_vectors *boot_vectors = &RPU_MCU_BOOT_VECTORS[rpu_proc];

	if (rpu_proc == RPU_PROC_TYPE_MCU_LMAC) {
		boot_sig_addr = RPU_MEM_LMAC_BOOT_SIG;
		run_addr = RPU_REG_MIPS_MCU_CONTROL;
		if (is_patch_present) {
			sleepctrl_addr = RPU_REG_UCC_SLEEP_CTRL_DATA_0;
			sleepctrl_val = NRF_WIFI_LMAC_ROM_PATCH_OFFSET;
		}
	} else if (rpu_proc == RPU_PROC_TYPE_MCU_UMAC) {
		boot_sig_addr = RPU_MEM_UMAC_BOOT_SIG;
		run_addr = RPU_REG_MIPS_MCU2_CONTROL;
		if (is_patch_present) {
			sleepctrl_addr = RPU_REG_UCC_SLEEP_CTRL_DATA_1;
			sleepctrl_val = NRF_WIFI_UMAC_ROM_PATCH_OFFSET;
		}
	} else {
		nrf_wifi_osal_log_err("%s: Invalid RPU processor type %d",
				      __func__,
				      rpu_proc);
		goto out;
	}

	/* Set the HAL RPU context to the current required context */
	hal_dev_ctx->curr_proc = rpu_proc;

	/* Clear the firmware pass signature location */
	status = hal_rpu_mem_write(hal_dev_ctx,
				   boot_sig_addr,
				   &boot_sig_val,
				   sizeof(boot_sig_val));

	if (status != NRF_WIFI_STATUS_SUCCESS) {
		nrf_wifi_osal_log_err("%s: Clearing of FW pass signature failed for RPU(%d)",
				      __func__,
				      rpu_proc);

		goto out;
	}

	if (is_patch_present) {
		/* Write to sleep control register */
		status = hal_rpu_reg_write(hal_dev_ctx,
					   sleepctrl_addr,
					   sleepctrl_val);
		if (status != NRF_WIFI_STATUS_SUCCESS) {
			nrf_wifi_osal_log_err("%s: Sleep control reg write failed for RPU(%d)",
					      __func__,
					      rpu_proc);

			goto out;
		}
	}

	for (boot_vector_id = 0; boot_vector_id < ARRAY_SIZE(boot_vectors->vectors); boot_vector_id++) {
		const struct rpu_mcu_boot_vector *boot_vector = &boot_vectors->vectors[boot_vector_id];

		/* Write the boot vector to the RPU memory */
		status = hal_rpu_reg_write(hal_dev_ctx,
					   boot_vector->addr,
					   boot_vector->val);
		if (status != NRF_WIFI_STATUS_SUCCESS) {
			nrf_wifi_osal_log_err("%s: Writing boot vector failed for RPU(%d)",
					      __func__,
					      rpu_proc);

			goto out;
		}
	}

	/* Perform pulsed soft reset of MIPS - this should now run */
	status = hal_rpu_reg_write(hal_dev_ctx,
				   run_addr,
				   0x1);

	if (status != NRF_WIFI_STATUS_SUCCESS) {
		nrf_wifi_osal_log_err("%s: RPU processor(%d) run failed",
				      __func__,
				      rpu_proc);

		goto out;
	}
out:
	/* Reset the HAL RPU context to the LMAC context */
	hal_dev_ctx->curr_proc = RPU_PROC_TYPE_MCU_LMAC;

	return status;

}
