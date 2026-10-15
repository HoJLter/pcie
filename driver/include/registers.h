#ifndef REGISTERS_H
#define REGISTERS_H

/* КОНФИГУРАЦИОННЫЕ РЕГИСТРЫ IP XDMA */
#define IRQ_BLOCK_OFS (0x2 << 12)
#define IRQ_BLOCK_ID_OFS 0x0
#define IRQ_ENABLE_W1S_OFS 0x08
#define IRQ_ENABLE_W1C_OFS 0x0C
#define IRQ_USER_VECTOR_OFS 0x80


/* РЕГИСТРЫ ПОЛЬЗОВАТЕЛЬСКОЙ ЛОГИКИ */

/* Регистр маски прерывания */
#define MASK_REG_OFS 0x00

/**
 * Конфигурационный регистр пользователськой логики.
 *  
 * [1:0] - биты, отвечающие за режим детекции:
 *       01 - реагировать только на восходящий фронт
 *       10 - реагировать только на нисходящий фронт
 *       11 - реагировать на любой фронт
 *       00 - не реагировать
 * 
 * [2] - бит, отвечающий за подтверждение обработки прерывания.
 */
#define CONTROL_REG_OFS 0x04

#define DETECT_MODE_MASK (BIT(0) | BIT(1))
#define HANDLED_ACK_MASK (BIT(2))
#define DETECT_MODE_MAX 3
#endif