ARMGNU ?= aarch64-linux-gnu

# Build output directory (passed from parent Makefile)
BUILD_DIR ?= $(CURDIR)/../../build/tools/md

all: $(BUILD_DIR)/md.q

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/md.q: md.c | $(BUILD_DIR)
	$(ARMGNU)-gcc -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
