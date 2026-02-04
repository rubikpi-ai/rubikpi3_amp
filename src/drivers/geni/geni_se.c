/*
 * QCOM GENI Serial Engine common driver for baremetal
 *
 * This driver provides common initialization and helper functions
 * for GENI Serial Engine, shared by UART, I2C, SPI drivers.
 *
 * Reference: Linux kernel drivers/soc/qcom/qcom-geni-se.c
 */

#include <type.h>
#include <geni_se.h>

/**
 * geni_se_get_qup_hw_version() - Read the QUP wrapper Hardware version
 * @wrapper_base: Base address of QUP wrapper
 *
 * Return: Hardware Version of the wrapper
 */
u32 geni_se_get_qup_hw_version(u64 wrapper_base)
{
	return geni_read32(wrapper_base, QUPV3_HW_VER_REG);
}

/**
 * geni_se_read_proto() - Read the protocol configured for a serial engine
 * @se_base: Base address of the serial engine
 *
 * Return: Protocol value (enum geni_se_protocol_type)
 */
u32 geni_se_read_proto(u64 se_base)
{
	u32 val = geni_read32(se_base, GENI_FW_REVISION_RO);
	return (val & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT;
}

/**
 * geni_se_get_tx_fifo_depth() - Get the TX fifo depth
 * @se_base: Base address of the serial engine
 *
 * Return: TX fifo depth in units of FIFO words
 */
u32 geni_se_get_tx_fifo_depth(u64 se_base)
{
	u32 val = geni_read32(se_base, SE_HW_PARAM_0);
	return (val & TX_FIFO_DEPTH_MSK) >> TX_FIFO_DEPTH_SHFT;
}

/**
 * geni_se_get_rx_fifo_depth() - Get the RX fifo depth
 * @se_base: Base address of the serial engine
 *
 * Return: RX fifo depth in units of FIFO words
 */
u32 geni_se_get_rx_fifo_depth(u64 se_base)
{
	u32 val = geni_read32(se_base, SE_HW_PARAM_1);
	return (val & RX_FIFO_DEPTH_MSK) >> RX_FIFO_DEPTH_SHFT;
}

/**
 * geni_se_irq_clear() - Clear all pending IRQs
 * @se_base: Base address of the serial engine
 */
void geni_se_irq_clear(u64 se_base)
{
	geni_write32(se_base, SE_GSI_EVENT_EN, 0);
	geni_write32(se_base, SE_GENI_M_IRQ_CLEAR, 0xffffffff);
	geni_write32(se_base, SE_GENI_S_IRQ_CLEAR, 0xffffffff);
	geni_write32(se_base, SE_DMA_TX_IRQ_CLR, 0xffffffff);
	geni_write32(se_base, SE_DMA_RX_IRQ_CLR, 0xffffffff);
	geni_write32(se_base, SE_IRQ_EN, 0xffffffff);
}

/**
 * geni_se_io_init() - Initialize SE IO configuration
 * @se_base: Base address of the serial engine
 */
static void geni_se_io_init(u64 se_base)
{
	u32 val;

	/* Enable clock gating */
	val = geni_read32(se_base, SE_GENI_CGC_CTRL);
	val |= DEFAULT_CGC_EN;
	geni_write32(se_base, SE_GENI_CGC_CTRL, val);

	/* Enable DMA clocks */
	val = geni_read32(se_base, SE_DMA_GENERAL_CFG);
	val |= AHB_SEC_SLV_CLK_CGC_ON | DMA_AHB_SLV_CLK_CGC_ON;
	val |= DMA_TX_CLK_CGC_ON | DMA_RX_CLK_CGC_ON;
	geni_write32(se_base, SE_DMA_GENERAL_CFG, val);

	/* Set default IO output control */
	geni_write32(se_base, GENI_OUTPUT_CTRL, DEFAULT_IO_OUTPUT_CTRL_MSK);
	geni_write32(se_base, GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);
}

/**
 * geni_se_io_set_mode() - Set SE IO mode (FIFO)
 * @se_base: Base address of the serial engine
 */
static void geni_se_io_set_mode(u64 se_base)
{
	u32 val;

	/* Enable GENI M/S IRQs and DMA IRQs */
	val = geni_read32(se_base, SE_IRQ_EN);
	val |= GENI_M_IRQ_EN | GENI_S_IRQ_EN;
	val |= DMA_TX_IRQ_EN | DMA_RX_IRQ_EN;
	geni_write32(se_base, SE_IRQ_EN, val);

	/* Disable DMA mode, use FIFO */
	val = geni_read32(se_base, SE_GENI_DMA_MODE_EN);
	val &= ~GENI_DMA_MODE_EN;
	geni_write32(se_base, SE_GENI_DMA_MODE_EN, val);

	/* Disable GSI events */
	geni_write32(se_base, SE_GSI_EVENT_EN, 0);
}

/**
 * geni_se_init() - Initialize the GENI serial engine
 * @se_base: Base address of the serial engine
 * @rx_wm: Receive watermark, in units of FIFO words
 * @rx_rfr: Ready-for-receive watermark, in units of FIFO words
 */
void geni_se_init(u64 se_base, u32 rx_wm, u32 rx_rfr)
{
	u32 val;

	geni_se_irq_clear(se_base);
	geni_se_io_init(se_base);
	geni_se_io_set_mode(se_base);

	/* Set watermarks */
	geni_write32(se_base, SE_GENI_RX_WATERMARK_REG, rx_wm);
	geni_write32(se_base, SE_GENI_RX_RFR_WATERMARK_REG, rx_rfr);

	/* Enable common M_IRQs */
	val = geni_read32(se_base, SE_GENI_M_IRQ_EN);
	val |= M_COMMON_GENI_M_IRQ_EN;
	geni_write32(se_base, SE_GENI_M_IRQ_EN, val);

	/* Enable common S_IRQs */
	val = geni_read32(se_base, SE_GENI_S_IRQ_EN);
	val |= S_COMMON_GENI_S_IRQ_EN;
	geni_write32(se_base, SE_GENI_S_IRQ_EN, val);
}

/**
 * geni_se_select_fifo_mode() - Select FIFO mode
 * @se_base: Base address of the serial engine
 */
static void geni_se_select_fifo_mode(u64 se_base)
{
	u32 proto = geni_se_read_proto(se_base);
	u32 val, val_old;

	geni_se_irq_clear(se_base);

	/* UART driver manages enabling/disabling interrupts internally */
	if (proto != GENI_SE_UART) {
		/* Non-UART use only primary sequencer */
		val_old = val = geni_read32(se_base, SE_GENI_M_IRQ_EN);
		val |= M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN;
		val |= M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN;
		if (val != val_old)
			geni_write32(se_base, SE_GENI_M_IRQ_EN, val);
	}

	/* Disable DMA mode */
	val_old = val = geni_read32(se_base, SE_GENI_DMA_MODE_EN);
	val &= ~GENI_DMA_MODE_EN;
	if (val != val_old)
		geni_write32(se_base, SE_GENI_DMA_MODE_EN, val);
}

/**
 * geni_se_select_dma_mode() - Select DMA mode
 * @se_base: Base address of the serial engine
 */
static void geni_se_select_dma_mode(u64 se_base)
{
	u32 proto = geni_se_read_proto(se_base);
	u32 val, val_old;

	geni_se_irq_clear(se_base);

	/* UART driver manages enabling/disabling interrupts internally */
	if (proto != GENI_SE_UART) {
		/* Non-UART use only primary sequencer */
		val_old = val = geni_read32(se_base, SE_GENI_M_IRQ_EN);
		val &= ~(M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN);
		val &= ~(M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
		if (val != val_old)
			geni_write32(se_base, SE_GENI_M_IRQ_EN, val);
	}

	/* Enable DMA mode */
	val_old = val = geni_read32(se_base, SE_GENI_DMA_MODE_EN);
	val |= GENI_DMA_MODE_EN;
	if (val != val_old)
		geni_write32(se_base, SE_GENI_DMA_MODE_EN, val);
}

/**
 * geni_se_select_gpi_mode() - Select GPI DMA mode
 * @se_base: Base address of the serial engine
 */
static void geni_se_select_gpi_mode(u64 se_base)
{
	u32 val;

	geni_se_irq_clear(se_base);

	geni_write32(se_base, SE_IRQ_EN, 0);

	val = geni_read32(se_base, SE_GENI_M_IRQ_EN);
	val &= ~(M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN |
		 M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
	geni_write32(se_base, SE_GENI_M_IRQ_EN, val);

	geni_write32(se_base, SE_GENI_DMA_MODE_EN, GENI_DMA_MODE_EN);

	val = geni_read32(se_base, SE_GSI_EVENT_EN);
	val |= (DMA_RX_EVENT_EN | DMA_TX_EVENT_EN | GENI_M_EVENT_EN | GENI_S_EVENT_EN);
	geni_write32(se_base, SE_GSI_EVENT_EN, val);
}

/**
 * geni_se_select_mode() - Select the serial engine transfer mode
 * @se_base: Base address of the serial engine
 * @mode: Transfer mode (GENI_SE_FIFO, GENI_SE_DMA, etc.)
 */
void geni_se_select_mode(u64 se_base, enum geni_se_xfer_mode mode)
{
	switch (mode) {
	case GENI_SE_FIFO:
		geni_se_select_fifo_mode(se_base);
		break;
	case GENI_SE_DMA:
		geni_se_select_dma_mode(se_base);
		break;
	case GENI_GPI_DMA:
		geni_se_select_gpi_mode(se_base);
		break;
	case GENI_SE_INVALID:
	default:
		break;
	}
}

/**
 * geni_se_config_packing() - Configure packing for the serial engine
 * @se_base: Base address of the serial engine
 * @bpw: Bits per word
 * @pack_words: Number of words per FIFO entry
 * @msb_to_lsb: True for MSB to LSB, false for LSB to MSB
 * @tx_cfg: Configure TX packing
 * @rx_cfg: Configure RX packing
 */
void geni_se_config_packing(u64 se_base, int bpw, int pack_words,
			    bool msb_to_lsb, bool tx_cfg, bool rx_cfg)
{
	u32 cfg0, cfg1, cfg[NUM_PACKING_VECTORS] = {0};
	int len;
	int temp_bpw = bpw;
	int idx_start = msb_to_lsb ? bpw - 1 : 0;
	int idx = idx_start;
	int idx_delta = msb_to_lsb ? -BITS_PER_BYTE : BITS_PER_BYTE;
	int ceil_bpw = ALIGN(bpw, BITS_PER_BYTE);
	int iter = (ceil_bpw * pack_words) / BITS_PER_BYTE;
	int i;

	if (iter <= 0 || iter > NUM_PACKING_VECTORS)
		return;

	for (i = 0; i < iter; i++) {
		len = min(temp_bpw, BITS_PER_BYTE) - 1;
		cfg[i] = idx << PACKING_START_SHIFT;
		cfg[i] |= msb_to_lsb << PACKING_DIR_SHIFT;
		cfg[i] |= len << PACKING_LEN_SHIFT;

		if (temp_bpw <= BITS_PER_BYTE) {
			idx = ((i + 1) * BITS_PER_BYTE) + idx_start;
			temp_bpw = bpw;
		} else {
			idx = idx + idx_delta;
			temp_bpw = temp_bpw - BITS_PER_BYTE;
		}
	}
	cfg[iter - 1] |= PACKING_STOP_BIT;
	cfg0 = cfg[0] | (cfg[1] << PACKING_VECTOR_SHIFT);
	cfg1 = cfg[2] | (cfg[3] << PACKING_VECTOR_SHIFT);

	if (tx_cfg) {
		geni_write32(se_base, SE_GENI_TX_PACKING_CFG0, cfg0);
		geni_write32(se_base, SE_GENI_TX_PACKING_CFG1, cfg1);
	}
	if (rx_cfg) {
		geni_write32(se_base, SE_GENI_RX_PACKING_CFG0, cfg0);
		geni_write32(se_base, SE_GENI_RX_PACKING_CFG1, cfg1);
	}

	/*
	 * Number of protocol words in each FIFO entry
	 * 0 - 4x8, four words in each entry, max word size of 8 bits
	 * 1 - 2x16, two words in each entry, max word size of 16 bits
	 * 2 - 1x32, one word in each entry, max word size of 32 bits
	 * 3 - undefined
	 */
	if (pack_words || bpw == 32)
		geni_write32(se_base, SE_GENI_BYTE_GRAN, bpw / 16);
}

/**
 * geni_se_cancel_m_cmd() - Cancel the primary sequencer command
 * @se_base: Base address of the serial engine
 */
void geni_se_cancel_m_cmd(u64 se_base)
{
	geni_write32(se_base, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_CANCEL);
}

/**
 * geni_se_abort_m_cmd() - Abort the primary sequencer command
 * @se_base: Base address of the serial engine
 */
void geni_se_abort_m_cmd(u64 se_base)
{
	geni_write32(se_base, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
}

/**
 * geni_se_setup_s_cmd() - Setup secondary sequencer command
 * @se_base: Base address of the serial engine
 * @opcode: Operation code for the command
 * @params: Command parameters
 *
 * This function is used to start commands that use the secondary sequencer,
 * such as UART RX operations.
 */
void geni_se_setup_s_cmd(u64 se_base, u32 opcode, u32 params)
{
	u32 cmd = (opcode << S_OPCODE_SHFT) | (params & S_PARAMS_MSK);
	geni_write32(se_base, SE_GENI_S_CMD0, cmd);
}

/**
 * geni_se_cancel_s_cmd() - Cancel the secondary sequencer command
 * @se_base: Base address of the serial engine
 */
void geni_se_cancel_s_cmd(u64 se_base)
{
	geni_write32(se_base, SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_CANCEL);
}

/**
 * geni_se_abort_s_cmd() - Abort the secondary sequencer command
 * @se_base: Base address of the serial engine
 */
void geni_se_abort_s_cmd(u64 se_base)
{
	geni_write32(se_base, SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_ABORT);
}
