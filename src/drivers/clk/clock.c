#include <type.h>
#include <io.h>
#include <clock.h>

int clk_enable(int id)
{
	struct clk *clk = clock_resource[id];
	clk->ops->enable(clk);
}

int clk_set_rate(int id, u32 rate)
{
	struct clk *clk = clock_resource[id];

	if (clk->ops->set_rate)
		return clk->ops->set_rate(clk, rate);
	return -1;
}

int clk_disable(int id)
{
	struct clk *clk = clock_resource[id];
	clk->ops->disable(clk);
}
