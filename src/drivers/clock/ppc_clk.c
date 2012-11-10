/**
 * @file
 * @brief PowerPC Microprocessor Family clock device driver
 *
 * @date 06.11.12
 * @author Ilia Vaprol
 */

#include <types.h>

#include <drivers/irqctrl.h>
#include <hal/clock.h>
#include <hal/system.h>
#include <kernel/irq.h>
#include <kernel/time/clock_source.h>
#include <prom/prom_printf.h>

#include <embox/unit.h>

EMBOX_UNIT_INIT(ppc_clk_init);

/*static*/ irq_return_t clock_handler(unsigned int irq_nr, void *data) {
	prom_printf("tick!\n");
	return IRQ_HANDLED;
}

static int ppc_clk_config(struct time_dev_conf *conf) {
	return 0;
}

static cycle_t ppc_clk_read(void) {
	return 0;
}

static struct time_event_device ppc_clk_event = {
	.config = ppc_clk_config,
//	.resolution = 1000,
//	.irq_nr = GPTIMER1_IRQ,
};


static struct time_counter_device ppc_clk_counter = {
	.read = ppc_clk_read,
//	.resolution = SYS_CLOCK / CLOCK_DIVIDER,
};

/*static*/ struct clock_source ppc_clk_clock_source = {
	.name = "ppc_clk",
	.event_device = &ppc_clk_event,
	.counter_device = &ppc_clk_counter,
//	.read = clock_source_read,
};

static int ppc_clk_init(void) {
    __set_tcr(TCR_WP_21 | TCR_WRC_NO | TCR_WIE | TCR_DIE | TCR_FP_13 | TCR_FIE | TCR_ARE);
	__set_dec((uint32_t)-1);
//	clock_source_register(&ppc_clk_clock_source);
//	return irq_attach(30/*GPTIMER1_IRQ*/, clock_handler, 0, NULL, "ppc_clk");
	return 0;
}

