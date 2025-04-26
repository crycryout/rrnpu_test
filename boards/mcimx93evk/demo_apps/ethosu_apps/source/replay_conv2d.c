#include "replay_templates_conv2d.h"
#include <stdio.h>
#include "ethosu_log.h"
void print_memory(const void *addr, size_t len) {
    const uint8_t *p = (const uint8_t *)addr;      // 将地址转换为字节指针
    for (size_t i = 0; i < len; i++) {
        PRINTF("%02X ", p[i]);                     // 以两位十六进制打印每个字节 :contentReference[oaicite:0]{index=0}
        if ((i + 1) % 16 == 0) {
            PRINTF("\r\n");                        // 每 16 字节换行
        }
    }
    if (len % 16 != 0) {
        PRINTF("\r\n");
    }
}

void replay_handle_interrupt(void)
{
    for (int i = INTERRUPT_HANDLING_START; i < INTERRUPT_HANDLING_END; ++i) {
        reg_op_record_t *rec = &register_access_records[i];
        volatile uint32_t *addr = (volatile uint32_t *)rec->reg_address;

        if (rec->op_type == REG_OP_WRITE) {
            *addr = rec->reg_value;
            LOG_INFO("IRQ WRITE: Addr=%p, Val=0x%08x, Order=%u\n",
                     addr, rec->reg_value, rec->op_order);
        } else {
            uint32_t v;
            /* 对 45 号读操作特殊处理：任何非 0 都视为“中断到来” */
            uint32_t expected = rec->reg_value;
            if (rec->op_order == 45) {
                expected = 1;
            }

            /* —— 轮询直到读到和期望一致 —— */
            do {
                v = *addr;
                if (v != expected) {
                    LOG_INFO("IRQ POLL: Addr=%p, Got=0x%08x, Expect=0x%08x, Order=%u\n",
                             addr, v, expected, rec->op_order);
                }
            } while (v != expected);

            LOG_INFO("IRQ READ OK: Addr=%p, Val=0x%08x, Order=%u\n",
                     addr, v, rec->op_order);
        }
    }

    /* —— 重放完 48 条之后，把结果数据 dump 出来 —— */
    LOG_INFO("INFERENCE RESULT (128 bytes):");
    LOG_INFO("print_result:");
 //   const uintptr_t target_addr = 0x20484000UL;
//    print_memory((const void *)target_addr, 1552);
    LOG_INFO("\n");
    PRINTF("INTERRUPTS OK NOW!");
}


/**
 *  重新精简：不要在这里再做 IRQ 跳过
 */
uint32_t register_access(reg_op_record_t *record) {
    uint32_t result = 0;

    /* —— 根据 op_order 预写数据 —— */
    switch (record->op_order) {
        case 24:
            memcpy((void *)0x20480200, op_24_data, sizeof(op_24_data));
            break;
        case 28:
            memcpy((void *)0x20480110, op_28_model_record_data, sizeof(op_28_model_record_data));
            break;
        case 31:
            memcpy((void *)0x20484000, op_31_data, sizeof(op_31_data));
            break;
        case 34:
            memcpy((void *)0x20480000, op_34model_head_data, sizeof(op_34model_head_data));
            break;
        case 37:
            memcpy((void *)0x20484050, op_37_input_record_data, sizeof(op_37_input_record_data));
            break;
        case 40:
            memcpy((void *)0x20484000, op_40_data, sizeof(op_40_data));
            break;
        default:
            break;
    }

    if (record->op_type == REG_OP_WRITE) {
        *(volatile uint32_t *)record->reg_address = record->reg_value;
        LOG_INFO("WRITE: Addr=%p, Val=0x%08x, Order=%u\n",
                 record->reg_address, record->reg_value, record->op_order);
        result = record->reg_value;
    } else {
        uint32_t read_v = *(volatile uint32_t *)record->reg_address;
        LOG_INFO("READ: Addr=%p, Got=0x%08x, Expect=0x%08x, Order=%u\n",
                 record->reg_address, read_v, record->reg_value, record->op_order);
        if (read_v == record->reg_value) {
            LOG_INFO("  -> MATCH\n");
        } else {
            LOG_INFO("  -> MISMATCH\n");
        }
        result = read_v;
    }

    return result;
}



//replay templates of initialization & verification
void replay_initialization_verification(void) {
    for (int i = INIT_VERIFICATION_START; i <= INIT_VERIFICATION_END; ++i) {
            if (i != WAIT) {
                // 非轮询情形，直接调用 register_access
                register_access(&register_access_records[i]);
            }
            else {
                // WAIT 情形的处理
                int j;
                for (j = 0; j < 100000 && register_access(&register_access_records[i]); j++) {
                    // 空循环等待
                }
                if (register_access(&register_access_records[i]) != 0) {
                    LOG_ERR("failed to initialize Ethos\n");
                }
            }
        
    }
}


void replay_inference(void){
    for (int i = RUN_STREAM_COMMAND_START; i <= RUN_STREAM_COMMAND_END; ++i) {
            register_access(&register_access_records[i]);
    }
}

