#ifndef __UART_GENI_H
#define __UART_GENI_H
#include <type.h>

/*
 * UART2: serial@988000
 * reg = <0 0x00988000 0 0x4000>
 *
 * This driver assumes Linux already:
 *  - enabled clocks / power domain / interconnect votes
 *  - configured pinmux (gpio10/gpio11 to qup02)
 *  - set up GENI UART protocol engine
 *
 * Baremetal only does polled FIFO TX/RX.
 */
#define UART2_BASE 0x00988000ULL
#define QUPV3_WRAP0_BASE 0x009c0000UL

/* Common SE registers */
#define GENI_FORCE_DEFAULT_REG      0x20
#define GENI_OUTPUT_CTRL            0x24
#define SE_GENI_STATUS              0x40
#define GENI_SER_M_CLK_CFG          0x48
#define GENI_SER_S_CLK_CFG          0x4c
#define GENI_IF_DISABLE_RO          0x64
#define GENI_FW_REVISION_RO         0x68
#define SE_GENI_CLK_SEL             0x7c
#define SE_GENI_CFG_SEQ_START       0x84
#define SE_GENI_DMA_MODE_EN         0x258

#define SE_GENI_M_CMD0              0x600
#define SE_GENI_M_CMD_CTRL_REG      0x604
#define SE_GENI_M_IRQ_STATUS        0x610
#define SE_GENI_M_IRQ_CLEAR         0x618
#define SE_GENI_M_IRQ_EN            0x614

#define SE_GENI_S_CMD0              0x630
#define SE_GENI_S_CMD_CTRL_REG      0x634
#define SE_GENI_S_IRQ_STATUS        0x640
#define SE_GENI_S_IRQ_CLEAR         0x648
#define SE_GENI_S_IRQ_EN            0x644

#define SE_GENI_TX_FIFOn            0x700
#define SE_GENI_RX_FIFOn            0x780
#define SE_GENI_TX_FIFO_STATUS      0x800
#define SE_GENI_RX_FIFO_STATUS      0x804

#define SE_HW_PARAM_0               0xe24
#define SE_HW_PARAM_1               0xe28

/* UART specific regs */
#define SE_UART_TX_TRANS_CFG        0x25c
#define SE_UART_TX_WORD_LEN         0x268
#define SE_UART_TX_STOP_BIT_LEN     0x26c
#define SE_UART_TX_TRANS_LEN        0x270
#define SE_UART_TX_PARITY_CFG       0x2a4

#define SE_UART_RX_TRANS_CFG        0x280
#define SE_UART_RX_WORD_LEN         0x28c
#define SE_UART_RX_STALE_CNT        0x294
#define SE_UART_RX_PARITY_CFG       0x2a8

/* Fields */
#define FORCE_DEFAULT               (1U << 0)
#define GENI_IO_MUX_0_EN            (1U << 0)

#define START_TRIGGER               (1U << 0)

#define FW_REV_PROTOCOL_MSK         (0xFFU << 8)
#define FW_REV_PROTOCOL_SHFT        8
#define GENI_SE_UART                2

#define M_GENI_CMD_ACTIVE           (1U << 0)
#define S_GENI_CMD_ACTIVE           (1U << 12)

#define SER_CLK_EN                  (1U << 0)

/* M_CMD_CTRL bits */
#define M_GENI_DISABLE              (1U << 0)
#define M_GENI_CMD_ABORT            (1U << 1)
#define M_GENI_CMD_CANCEL           (1U << 2)

/* S_CMD_CTRL bits */
#define S_GENI_DISABLE              (1U << 0)
#define S_GENI_CMD_ABORT            (1U << 1)
#define S_GENI_CMD_CANCEL           (1U << 2)

/* IRQ bits */
#define M_CMD_DONE_EN               (1U << 0)

/* FIFO status fields */
#define TX_FIFO_WC_MASK             0x0FFFFFFF  /* GENMASK(27,0) from geni-se.h */
#define RX_FIFO_WC_MASK             0x01FFFFFF  /* GENMASK(24,0) from geni-se.h */

/* UART M_CMD opcode */
#define UART_START_TX               0x1
#define M_OPCODE_SHFT               27

#define FORCE_DEFAULT               (1U << 0)
#define GENI_IO_MUX_0_EN            (1U << 0)
#define START_TRIGGER               (1U << 0)

#define FW_REV_PROTOCOL_MSK         (0xFFU << 8)
#define FW_REV_PROTOCOL_SHFT        8
#define GENI_SE_UART                2

#define SER_CLK_EN                  (1U << 0)
#define CLK_DIV_SHFT                4

/* UART cfg bits from Linux */
#define UART_TX_PAR_EN              (1U << 0)
#define UART_CTS_MASK               (1U << 1)
#define UART_RX_PAR_EN              (1U << 3)
#define TX_STOP_BIT_LEN_1           0

#define SER_CLK_EN                  (1U << 0)
#define CLK_DIV_SHFT                4
#define CLK_DIV_MSK                 (0xFFFU << CLK_DIV_SHFT)  /* conservative; Linux uses div field */
#define CLK_SEL_MSK                 0x7

#define QUP_SE_VERSION_2_5 0x20050000

#define DEF_FIFO_DEPTH_WORDS		16
#define UART_RX_WM			2


/* Common QUPV3 registers */
#define QUPV3_HW_VER_REG		0x4
#define QUPV3_SE_AHB_M_CFG		0x118
#define QUPV3_COMMON_CFG		0x120
#define QUPV3_COMMON_CGC_CTRL		0x21c

/* QUPV3_COMMON_CFG fields */
#define FAST_SWITCH_TO_HIGH_DISABLE	BIT(0)

/* QUPV3_SE_AHB_M_CFG fields */
#define AHB_M_CLK_CGC_ON		BIT(0)

/* QUPV3_COMMON_CGC_CTRL fields */
#define COMMON_CSR_SLV_CLK_CGC_ON	BIT(0)

/* Common SE registers */
#define SE_GENI_INIT_CFG_REVISION	0x0
#define SE_GENI_S_INIT_CFG_REVISION	0x4
#define SE_GENI_CGC_CTRL		0x28
#define SE_GENI_CLK_CTRL_RO		0x60
#define SE_GENI_FW_S_REVISION_RO	0x6c
#define SE_GENI_CFG_REG0		0x100
#define SE_GENI_BYTE_GRAN		0x254
#define SE_GENI_TX_PACKING_CFG0		0x260
#define SE_GENI_TX_PACKING_CFG1		0x264
#define SE_GENI_RX_PACKING_CFG0		0x284
#define SE_GENI_RX_PACKING_CFG1		0x288
#define SE_GENI_S_IRQ_ENABLE		0x644
#define SE_DMA_TX_PTR_L			0xc30
#define SE_DMA_TX_PTR_H			0xc34
#define SE_DMA_TX_ATTR			0xc38
#define SE_DMA_TX_LEN			0xc3c
#define SE_DMA_TX_IRQ_EN		0xc48
#define SE_DMA_TX_IRQ_EN_SET		0xc4c
#define SE_DMA_TX_IRQ_EN_CLR		0xc50
#define SE_DMA_TX_LEN_IN		0xc54
#define SE_DMA_TX_MAX_BURST		0xc5c
#define SE_DMA_RX_PTR_L			0xd30
#define SE_DMA_RX_PTR_H			0xd34
#define SE_DMA_RX_ATTR			0xd38
#define SE_DMA_RX_LEN			0xd3c
#define SE_DMA_RX_IRQ_EN		0xd48
#define SE_DMA_RX_IRQ_EN_SET		0xd4c
#define SE_DMA_RX_IRQ_EN_CLR		0xd50
#define SE_DMA_RX_MAX_BURST		0xd5c
#define SE_DMA_RX_FLUSH			0xd60
#define SE_GSI_EVENT_EN			0xe18
#define SE_IRQ_EN			0xe1c
#define SE_DMA_GENERAL_CFG		0xe30
#define SE_GENI_FW_REVISION		0x1000
#define SE_GENI_FW_REVISION		0x1000
#define SE_GENI_S_FW_REVISION		0x1004
#define SE_GENI_CFG_RAMN		0x1010
#define SE_GENI_CLK_CTRL		0x2000
#define SE_DMA_IF_EN			0x2004
#define SE_FIFO_IF_DISABLE		0x2008
#define GENI_FORCE_DEFAULT_REG		0x20
#define GENI_OUTPUT_CTRL		0x24
#define SE_GENI_STATUS			0x40
#define GENI_SER_M_CLK_CFG		0x48
#define GENI_SER_S_CLK_CFG		0x4c
#define GENI_IF_DISABLE_RO		0x64
#define GENI_FW_REVISION_RO		0x68
#define SE_GENI_CLK_SEL			0x7c
#define SE_GENI_CFG_SEQ_START		0x84
#define SE_GENI_DMA_MODE_EN		0x258
#define SE_GENI_M_CMD0			0x600
#define SE_GENI_M_CMD_CTRL_REG		0x604
#define SE_GENI_M_IRQ_STATUS		0x610
#define SE_GENI_M_IRQ_EN		0x614
#define SE_GENI_M_IRQ_CLEAR		0x618
#define SE_GENI_M_IRQ_EN_SET		0x61c
#define SE_GENI_M_IRQ_EN_CLEAR		0x620
#define SE_GENI_S_CMD0			0x630
#define SE_GENI_S_CMD_CTRL_REG		0x634
#define SE_GENI_S_IRQ_STATUS		0x640
#define SE_GENI_S_IRQ_EN		0x644
#define SE_GENI_S_IRQ_CLEAR		0x648
#define SE_GENI_S_IRQ_EN_SET		0x64c
#define SE_GENI_S_IRQ_EN_CLEAR		0x650
#define SE_GENI_TX_FIFOn		0x700
#define SE_GENI_RX_FIFOn		0x780
#define SE_GENI_TX_FIFO_STATUS		0x800
#define SE_GENI_RX_FIFO_STATUS		0x804
#define SE_GENI_TX_WATERMARK_REG	0x80c
#define SE_GENI_RX_WATERMARK_REG	0x810
#define SE_GENI_RX_RFR_WATERMARK_REG	0x814
#define SE_GENI_IOS			0x908
#define SE_GENI_M_GP_LENGTH		0x910
#define SE_GENI_S_GP_LENGTH		0x914
#define SE_DMA_TX_IRQ_STAT		0xc40
#define SE_DMA_TX_IRQ_CLR		0xc44
#define SE_DMA_TX_FSM_RST		0xc58
#define SE_DMA_RX_IRQ_STAT		0xd40
#define SE_DMA_RX_IRQ_CLR		0xd44
#define SE_DMA_RX_LEN_IN		0xd54
#define SE_DMA_RX_FSM_RST		0xd58
#define SE_HW_PARAM_0			0xe24
#define SE_HW_PARAM_1			0xe28


/* GENI_FW_REVISION_RO fields */
#define FW_REV_VERSION_MSK		GENMASK(7, 0)

/* GENI_OUTPUT_CTRL fields */
#define DEFAULT_IO_OUTPUT_CTRL_MSK	GENMASK(6, 0)

/* GENI_CGC_CTRL fields */
#define CFG_AHB_CLK_CGC_ON		BIT(0)
#define CFG_AHB_WR_ACLK_CGC_ON		BIT(1)
#define DATA_AHB_CLK_CGC_ON		BIT(2)
#define SCLK_CGC_ON			BIT(3)
#define TX_CLK_CGC_ON			BIT(4)
#define RX_CLK_CGC_ON			BIT(5)
#define EXT_CLK_CGC_ON			BIT(6)
#define PROG_RAM_HCLK_OFF		BIT(8)
#define PROG_RAM_SCLK_OFF		BIT(9)
#define DEFAULT_CGC_EN			GENMASK(6, 0)

/* SE_GSI_EVENT_EN fields */
#define DMA_RX_EVENT_EN			BIT(0)
#define DMA_TX_EVENT_EN			BIT(1)
#define GENI_M_EVENT_EN			BIT(2)
#define GENI_S_EVENT_EN			BIT(3)

/* SE_IRQ_EN fields */
#define DMA_RX_IRQ_EN			BIT(0)
#define DMA_TX_IRQ_EN			BIT(1)
#define GENI_M_IRQ_EN			BIT(2)
#define GENI_S_IRQ_EN			BIT(3)

/* SE_DMA_GENERAL_CFG */
#define DMA_RX_CLK_CGC_ON		BIT(0)
#define DMA_TX_CLK_CGC_ON		BIT(1)
#define DMA_AHB_SLV_CLK_CGC_ON		BIT(2)
#define AHB_SEC_SLV_CLK_CGC_ON		BIT(3)
#define DUMMY_RX_NON_BUFFERABLE		BIT(4)
#define RX_DMA_ZERO_PADDING_EN		BIT(5)
#define RX_DMA_IRQ_DELAY_MSK		GENMASK(8, 6)
#define RX_DMA_IRQ_DELAY_SHFT		6

/* GENI_CLK_CTRL fields */
#define SER_CLK_SEL			BIT(0)
#define DMA_IF_EN			BIT(0)

#define GENI_DMA_MODE_EN		BIT(0)

/* GENI_M_IRQ_EN fields */
#define M_CMD_DONE_EN			BIT(0)
#define M_CMD_OVERRUN_EN		BIT(1)
#define M_ILLEGAL_CMD_EN		BIT(2)
#define M_CMD_FAILURE_EN		BIT(3)
#define M_CMD_CANCEL_EN			BIT(4)
#define M_CMD_ABORT_EN			BIT(5)
#define M_TIMESTAMP_EN			BIT(6)
#define M_RX_IRQ_EN			BIT(7)
#define M_GP_SYNC_IRQ_0_EN		BIT(8)
#define M_GP_IRQ_0_EN			BIT(9)
#define M_GP_IRQ_1_EN			BIT(10)
#define M_GP_IRQ_2_EN			BIT(11)
#define M_GP_IRQ_3_EN			BIT(12)
#define M_GP_IRQ_4_EN			BIT(13)
#define M_GP_IRQ_5_EN			BIT(14)
#define M_TX_FIFO_NOT_EMPTY_EN		BIT(21)
#define M_IO_DATA_DEASSERT_EN		BIT(22)
#define M_IO_DATA_ASSERT_EN		BIT(23)
#define M_RX_FIFO_RD_ERR_EN		BIT(24)
#define M_RX_FIFO_WR_ERR_EN		BIT(25)
#define M_RX_FIFO_WATERMARK_EN		BIT(26)
#define M_RX_FIFO_LAST_EN		BIT(27)
#define M_TX_FIFO_RD_ERR_EN		BIT(28)
#define M_TX_FIFO_WR_ERR_EN		BIT(29)
#define M_TX_FIFO_WATERMARK_EN		BIT(30)
#define M_SEC_IRQ_EN			BIT(31)
#define M_COMMON_GENI_M_IRQ_EN	(GENMASK(6, 1) | \
				M_IO_DATA_DEASSERT_EN | \
				M_IO_DATA_ASSERT_EN | M_RX_FIFO_RD_ERR_EN | \
				M_RX_FIFO_WR_ERR_EN | M_TX_FIFO_RD_ERR_EN | \
				M_TX_FIFO_WR_ERR_EN)

/* GENI_S_IRQ_EN fields */
#define S_CMD_DONE_EN			BIT(0)
#define S_CMD_OVERRUN_EN		BIT(1)
#define S_ILLEGAL_CMD_EN		BIT(2)
#define S_CMD_FAILURE_EN		BIT(3)
#define S_CMD_CANCEL_EN			BIT(4)
#define S_CMD_ABORT_EN			BIT(5)
#define S_GP_SYNC_IRQ_0_EN		BIT(8)
#define S_GP_IRQ_0_EN			BIT(9)
#define S_GP_IRQ_1_EN			BIT(10)
#define S_GP_IRQ_2_EN			BIT(11)
#define S_GP_IRQ_3_EN			BIT(12)
#define S_GP_IRQ_4_EN			BIT(13)
#define S_GP_IRQ_5_EN			BIT(14)
#define S_IO_DATA_DEASSERT_EN		BIT(22)
#define S_IO_DATA_ASSERT_EN		BIT(23)
#define S_RX_FIFO_RD_ERR_EN		BIT(24)
#define S_RX_FIFO_WR_ERR_EN		BIT(25)
#define S_RX_FIFO_WATERMARK_EN		BIT(26)
#define S_RX_FIFO_LAST_EN		BIT(27)
#define S_COMMON_GENI_S_IRQ_EN	(GENMASK(5, 1) | GENMASK(13, 9) | \
				 S_RX_FIFO_RD_ERR_EN | S_RX_FIFO_WR_ERR_EN)

#define STALE_TIMEOUT                  16
#define DEFAULT_BITS_PER_CHAR          10

/* Optionally read HW fifo depth if you want; simplest is fixed threshold */
#define UART_TX_FIFO_DEPTH_WORDS_DEFAULT 16

void uart2_putc(char c);
void uart2_puts(const char *s);
// int uart2_getc_nonblock(void);

/* Public API */
//void uart2_init_115200(void);
int  uart2_write(const void *buf, u32 len);   /* returns 0 on success */

/* Optional debug dump to shared memory */
void uart2_debug_dump(volatile u64 *shm, u32 shm_base_idx);

void uart2_debug_dump_and_try_tx(volatile u64 *shm, u32 shm_base_idx,
				const char *msg);
#endif
