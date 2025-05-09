/*
 * Copyright (c) 2019-2022 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ethosu_interface.h"

#include "ethosu_device.h"
#include "ethosu_log.h"

#ifdef ETHOSU55
#include "ethosu_config_u55.h"
#else
#include "ethosu_config_u65.h"
#endif

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define ETHOSU_PRODUCT_U55 0
#define ETHOSU_PRODUCT_U65 1

#define BASEP_OFFSET 4

#ifdef ETHOSU65
#define ADDRESS_BITS 40
#else
#define ADDRESS_BITS 32
#endif

#define ADDRESS_MASK ((1ull << ADDRESS_BITS) - 1)

#define NPU_CMD_PWR_CLK_MASK (0xC)

struct ethosu_device *ethosu_dev_init(const void *base_address, uint32_t secure_enable, uint32_t privilege_enable)
{
    struct ethosu_device *dev = malloc(sizeof(struct ethosu_device));
    if (!dev)
    {
        LOG_ERR("Failed to allocate memory for Ethos-U device");
        return NULL;
    }

    dev->reg        = (volatile struct NPU_REG *)base_address;
    dev->secure     = secure_enable;
    dev->privileged = privilege_enable;

#ifdef ETHOSU55
    if (dev->reg->CONFIG.product != ETHOSU_PRODUCT_U55)
#else
    if (dev->reg->CONFIG.product != ETHOSU_PRODUCT_U65)
#endif
    {
        LOG_ERR("Failed to initialize device. Driver has not been compiled for this product");
        goto err;
    }

    // Make sure the NPU is in a known state
    if (ethosu_dev_soft_reset(dev) != ETHOSU_SUCCESS)
    {
        goto err;
    }

    return dev;

err:
    free(dev);
    return NULL;
}

void ethosu_dev_deinit(struct ethosu_device *dev)
{
    free(dev);
}

enum ethosu_error_codes ethosu_dev_axi_init(struct ethosu_device *dev)
{
    struct regioncfg_r rcfg = {0};
    struct axi_limit0_r l0  = {0};
    struct axi_limit1_r l1  = {0};
    struct axi_limit2_r l2  = {0};
    struct axi_limit3_r l3  = {0};

    dev->reg->QCONFIG.word = NPU_QCONFIG;

    rcfg.region0             = NPU_REGIONCFG_0;
    rcfg.region1             = NPU_REGIONCFG_1;
    rcfg.region2             = NPU_REGIONCFG_2;
    rcfg.region3             = NPU_REGIONCFG_3;
    rcfg.region4             = NPU_REGIONCFG_4;
    rcfg.region5             = NPU_REGIONCFG_5;
    rcfg.region6             = NPU_REGIONCFG_6;
    rcfg.region7             = NPU_REGIONCFG_7;
    dev->reg->REGIONCFG.word = rcfg.word;

    l0.max_beats                = AXI_LIMIT0_MAX_BEATS_BYTES;
    l0.memtype                  = AXI_LIMIT0_MEM_TYPE;
    l0.max_outstanding_read_m1  = AXI_LIMIT0_MAX_OUTSTANDING_READS - 1;
    l0.max_outstanding_write_m1 = AXI_LIMIT0_MAX_OUTSTANDING_WRITES - 1;

    l1.max_beats                = AXI_LIMIT1_MAX_BEATS_BYTES;
    l1.memtype                  = AXI_LIMIT1_MEM_TYPE;
    l1.max_outstanding_read_m1  = AXI_LIMIT1_MAX_OUTSTANDING_READS - 1;
    l1.max_outstanding_write_m1 = AXI_LIMIT1_MAX_OUTSTANDING_WRITES - 1;

    l2.max_beats                = AXI_LIMIT2_MAX_BEATS_BYTES;
    l2.memtype                  = AXI_LIMIT2_MEM_TYPE;
    l2.max_outstanding_read_m1  = AXI_LIMIT2_MAX_OUTSTANDING_READS - 1;
    l2.max_outstanding_write_m1 = AXI_LIMIT2_MAX_OUTSTANDING_WRITES - 1;

    l3.max_beats                = AXI_LIMIT3_MAX_BEATS_BYTES;
    l3.memtype                  = AXI_LIMIT3_MEM_TYPE;
    l3.max_outstanding_read_m1  = AXI_LIMIT3_MAX_OUTSTANDING_READS - 1;
    l3.max_outstanding_write_m1 = AXI_LIMIT3_MAX_OUTSTANDING_WRITES - 1;

    dev->reg->AXI_LIMIT0.word = l0.word;
    dev->reg->AXI_LIMIT1.word = l1.word;
    dev->reg->AXI_LIMIT2.word = l2.word;
    dev->reg->AXI_LIMIT3.word = l3.word;

    return ETHOSU_SUCCESS;
}

void ethosu_dev_run_command_stream(struct ethosu_device *dev,
                                   const uint8_t *cmd_stream_ptr,
                                   uint32_t cms_length,
                                   const uint64_t *base_addr,
                                   int num_base_addr)
{
    assert(num_base_addr <= NPU_REG_BASEP_ARRLEN);

    struct cmd_r cmd;
    uint64_t qbase = (uintptr_t)cmd_stream_ptr + BASE_POINTER_OFFSET;
    assert(qbase <= ADDRESS_MASK);
    LOG_DEBUG("QBASE=0x%016llx, QSIZE=%u, base_pointer_offset=0x%08x", qbase, cms_length, BASE_POINTER_OFFSET);

    dev->reg->QBASE.word[0] = qbase & 0xffffffff;
#ifdef ETHOSU65
    dev->reg->QBASE.word[1] = qbase >> 32;
#endif
    dev->reg->QSIZE.word = cms_length;

    for (int i = 0; i < num_base_addr; i++)
    {
        uint64_t addr = base_addr[i] + BASE_POINTER_OFFSET;
        assert(addr <= ADDRESS_MASK);
        LOG_DEBUG("BASEP%d=0x%016llx", i, addr);
        dev->reg->BASEP[i].word[0] = addr & 0xffffffff;
#ifdef ETHOSU65
        dev->reg->BASEP[i].word[1] = addr >> 32;
#endif
    }

    cmd.word                        = dev->reg->CMD.word & NPU_CMD_PWR_CLK_MASK;
    cmd.transition_to_running_state = 1;

    dev->reg->CMD.word = cmd.word;
    LOG_DEBUG("CMD=0x%08x", cmd.word);

    LOG_INFO("Test Case 6: transition_to_running_state %u", cmd.transition_to_running_state);
}

void ethosu_dev_print_err_status(struct ethosu_device *dev)
{
    LOG_ERR("NPU status=0x%08" PRIx32 ", qread=%" PRIu32 ", cmd_end_reached=%d",
            dev->reg->STATUS.word,
            dev->reg->QREAD.word,
            dev->reg->STATUS.cmd_end_reached);
}

bool ethosu_dev_handle_interrupt(struct ethosu_device *dev)
{
    struct cmd_r cmd;

    // Clear interrupt
    cmd.word           = dev->reg->CMD.word & NPU_CMD_PWR_CLK_MASK;
    cmd.clear_irq      = 1;
    dev->reg->CMD.word = cmd.word;

    LOG_INFO("Test Case 10: bus_status_error 0x%x, status 0x%x",
	     dev->reg->STATUS.bus_status, dev->reg->STATUS.word);
    if (dev->reg->STATUS.bus_status == 1) {
        LOG_INFO("Test Case 10: faulting_inference 0x%x", dev->reg->STATUS.faulting_interface);
	LOG_INFO("Test Case 10: faulting_channel 0x%x", dev->reg->STATUS.faulting_channel);
    }
    LOG_INFO("Test Case 11: cmd_parse_error 0x%x", dev->reg->STATUS.cmd_parse_error);
    LOG_INFO("Test Case 12: wd_fault 0x%x", dev->reg->STATUS.wd_fault);
    LOG_INFO("Test Case 13: ecc_fault: 0x%x", dev->reg->STATUS.ecc_fault);
    LOG_INFO("Test Case 9: dma read/write to external memory");
    LOG_INFO("Test Case 14: cmd_end_reached 0x%x", dev->reg->STATUS.cmd_end_reached);
    LOG_INFO("Test Case 14: get read offset of command stream %lu",
	   dev->reg->QREAD.word);

    // If a fault has occured, the NPU needs to be reset
    if (dev->reg->STATUS.bus_status || dev->reg->STATUS.cmd_parse_error || dev->reg->STATUS.wd_fault ||
        dev->reg->STATUS.ecc_fault || !dev->reg->STATUS.cmd_end_reached)
    {
        return false;
    }

    return true;
}

bool ethosu_dev_verify_access_state(struct ethosu_device *dev)
{
    if (dev->reg->PROT.active_CSL != (dev->secure ? SECURITY_LEVEL_SECURE : SECURITY_LEVEL_NON_SECURE) ||
        dev->reg->PROT.active_CPL != (dev->privileged ? PRIVILEGE_LEVEL_PRIVILEGED : PRIVILEGE_LEVEL_USER))
    {
        return false;
    }
    return true;
}

enum ethosu_error_codes ethosu_dev_soft_reset(struct ethosu_device *dev)
{
    // Note that after a soft-reset, the NPU is unconditionally
    // powered until the next CMD gets written.

    struct reset_r reset;

    reset.word        = 0;
    reset.pending_CPL = dev->privileged ? PRIVILEGE_LEVEL_PRIVILEGED : PRIVILEGE_LEVEL_USER;
    reset.pending_CSL = dev->secure ? SECURITY_LEVEL_SECURE : SECURITY_LEVEL_NON_SECURE;

    // Reset and set security level
    LOG_INFO("Soft reset NPU");
    LOG_INFO("Test Case 7: soft reset NPU");
    dev->reg->RESET.word = reset.word;

    // Wait until reset status indicates that reset has been completed
    for (int i = 0; i < 100000 && dev->reg->STATUS.reset_status != 0; i++)
    {
    }

    if (dev->reg->STATUS.reset_status != 0)
    {
        LOG_ERR("Soft reset timed out");
        return ETHOSU_GENERIC_FAILURE;
    }

    LOG_INFO("Test Case 15: reset.pending_CPL 0x%x", reset.pending_CPL);
    LOG_INFO("Test Case 15: reset.pending_CSL 0x%x", reset.pending_CSL);
    // Verify that NPU has switched security state and privilege level
    if (ethosu_dev_verify_access_state(dev) != true)
    {
        LOG_ERR("Failed to switch security state and privilege level");
        return ETHOSU_GENERIC_FAILURE;
    }

    // Reinitialize AXI settings
    ethosu_dev_axi_init(dev);

    return ETHOSU_SUCCESS;
}

void ethosu_dev_get_hw_info(struct ethosu_device *dev, struct ethosu_hw_info *hwinfo)
{
    struct config_r cfg;
    struct id_r id;

    cfg.word = dev->reg->CONFIG.word;
    id.word  = dev->reg->ID.word;

    hwinfo->cfg.cmd_stream_version = cfg.cmd_stream_version;
    hwinfo->cfg.custom_dma         = cfg.custom_dma;
    hwinfo->cfg.macs_per_cc        = cfg.macs_per_cc;

    hwinfo->version.arch_major_rev = id.arch_major_rev;
    hwinfo->version.arch_minor_rev = id.arch_minor_rev;
    hwinfo->version.arch_patch_rev = id.arch_patch_rev;
    hwinfo->version.product_major  = id.product_major;
    hwinfo->version.version_major  = id.version_major;
    hwinfo->version.version_minor  = id.version_minor;
    hwinfo->version.version_status = id.version_status;

    /* Test Case 1-2 */
    LOG_INFO("Test Case 1-2: id.arch_major_rev %lu", id.arch_major_rev);
    LOG_INFO("Test Case 1-2: id.arch_minor_rev %lu", id.arch_minor_rev);
    LOG_INFO("Test Case 1-2: id.arch_patch_rev %lu", id.arch_patch_rev);
    LOG_INFO("Test Case 1-2: id.version_major %lu", id.version_major);
    LOG_INFO("Test Case 1-2: id.version_minor %lu", id.version_minor);
    LOG_INFO("Test Case 1-2: id.version_status %lu", id.version_status);
    LOG_INFO("Test Case 3: config.macs_per_cc %lu", cfg.macs_per_cc);
    LOG_INFO("Test Case 3: config.cmd_stream_verison %lu", cfg.cmd_stream_version);
    LOG_INFO("Test Case 3: config.shram_size %lu", cfg.shram_size);
}

enum ethosu_error_codes ethosu_dev_set_clock_and_power(struct ethosu_device *dev,
                                                       enum ethosu_clock_q_request clock_q,
                                                       enum ethosu_power_q_request power_q)
{
    struct cmd_r cmd = {0};
    cmd.word         = dev->reg->CMD.word & NPU_CMD_PWR_CLK_MASK;

    if (power_q != ETHOSU_POWER_Q_UNCHANGED)
    {
        cmd.power_q_enable = power_q == ETHOSU_POWER_Q_ENABLE ? 1 : 0;
    }
    if (clock_q != ETHOSU_CLOCK_Q_UNCHANGED)
    {
        cmd.clock_q_enable = clock_q == ETHOSU_CLOCK_Q_ENABLE ? 1 : 0;
    }

    dev->reg->CMD.word = cmd.word;
    LOG_DEBUG("CMD=0x%08x", cmd.word);

    LOG_INFO("Test Case 4: power_q_enable 0x%x", cmd.power_q_enable);
    LOG_INFO("Test Case 5: clock_q_enable 0x%x", cmd.clock_q_enable);
    return ETHOSU_SUCCESS;
}

bool ethosu_dev_verify_optimizer_config(struct ethosu_device *dev, uint32_t cfg_in, uint32_t id_in)
{
    struct config_r *opt_cfg = (struct config_r *)&cfg_in;
    struct config_r hw_cfg;
    struct id_r *opt_id = (struct id_r *)&id_in;
    struct id_r hw_id;
    bool ret = true;

    hw_cfg.word = dev->reg->CONFIG.word;
    hw_id.word  = dev->reg->ID.word;

    LOG_INFO("Optimizer config. product=%d, cmd_stream_version=%d, macs_per_cc=%d, shram_size=%d, custom_dma=%d",
             opt_cfg->product,
             opt_cfg->cmd_stream_version,
             opt_cfg->macs_per_cc,
             opt_cfg->shram_size,
             opt_cfg->custom_dma);
    LOG_INFO("Optimizer config. arch version: %d.%d.%d",
             opt_id->arch_major_rev,
             opt_id->arch_minor_rev,
             opt_id->arch_patch_rev);
    LOG_INFO("Ethos-U config. product=%d, cmd_stream_version=%d, macs_per_cc=%d, shram_size=%d, custom_dma=%d",
             hw_cfg.product,
             hw_cfg.cmd_stream_version,
             hw_cfg.macs_per_cc,
             hw_cfg.shram_size,
             hw_cfg.custom_dma);
    LOG_INFO("Ethos-U. arch version=%d.%d.%d", hw_id.arch_major_rev, hw_id.arch_minor_rev, hw_id.arch_patch_rev);

    if (opt_cfg->word != hw_cfg.word)
    {
        if (hw_cfg.product != opt_cfg->product)
        {
            LOG_ERR("NPU config mismatch. npu.product=%d, optimizer.product=%d", hw_cfg.product, opt_cfg->product);
            ret = false;
        }

        if (hw_cfg.macs_per_cc != opt_cfg->macs_per_cc)
        {
            LOG_ERR("NPU config mismatch. npu.macs_per_cc=%d, optimizer.macs_per_cc=%d",
                    hw_cfg.macs_per_cc,
                    opt_cfg->macs_per_cc);
            ret = false;
        }

        if (hw_cfg.cmd_stream_version != opt_cfg->cmd_stream_version)
        {
            LOG_ERR("NPU config mismatch. npu.cmd_stream_version=%d, optimizer.cmd_stream_version=%d",
                    hw_cfg.cmd_stream_version,
                    opt_cfg->cmd_stream_version);
            ret = false;
        }

        if (!hw_cfg.custom_dma && opt_cfg->custom_dma)
        {
            LOG_ERR("NPU config mismatch. npu.custom_dma=%d, optimizer.custom_dma=%d",
                    hw_cfg.custom_dma,
                    opt_cfg->custom_dma);
            ret = false;
        }
    }
    if (ret == true)
	LOG_INFO("Test Case 16: handle_optimizer_config: NPU config match");

    if ((hw_id.arch_major_rev != opt_id->arch_major_rev) || (hw_id.arch_minor_rev < opt_id->arch_minor_rev))
    {
        LOG_ERR("NPU arch mismatch. npu.arch=%d.%d.%d, optimizer.arch=%d.%d.%d",
                hw_id.arch_major_rev,
                hw_id.arch_minor_rev,
                hw_id.arch_patch_rev,
                opt_id->arch_major_rev,
                opt_id->arch_minor_rev,
                opt_id->arch_patch_rev);
        ret = false;
    }
    if (ret == true)
        LOG_INFO("Test Case 17: handle_optimizer_config: NPU arch match");

    return ret;
}
