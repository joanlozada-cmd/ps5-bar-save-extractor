# 1. SDK Setup
ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
$(error PS5_PAYLOAD_SDK is undefined)
endif

# 2. Paths and Variables
BIN_DIR := bin
CFLAGS := -Werror -Iinclude
LDFLAGS :=

# Original upstream sources
SRCS := source/main.c source/bar_srv.c

# Save-extractor sources (new fork feature)
SAVE_EXTRACTOR_SRCS := source/save_extractor.c source/bar_srv.c

# 3. Output Targets
ELF_INFO := $(BIN_DIR)/ps5-bar-tool_info.elf
ELF_SEGMENTS := $(BIN_DIR)/ps5-bar-tool_dump_main_segments.elf
ELF_SAVEDATA := $(BIN_DIR)/ps5-bar-tool_savedata.elf
ELF_ALL := $(BIN_DIR)/ps5-bar-tool_dump_all.elf
ELF_SAVE_EXTRACTOR := $(BIN_DIR)/ps5-bar-save-extractor.elf

# --- Target-Specific Flags for original upstream payloads ---
$(ELF_INFO): CFLAGS += -DDUMP_MAIN_SEGMENTS=0 -DDUMP_FILES=0
$(ELF_SEGMENTS): CFLAGS += -DDUMP_MAIN_SEGMENTS=1 -DDUMP_FILES=0
$(ELF_SAVEDATA): CFLAGS += -DDUMP_MAIN_SEGMENTS=0 -DDUMP_FILES=0 -DDUMP_SAVES=1
$(ELF_ALL): CFLAGS += -DDUMP_MAIN_SEGMENTS=1 -DDUMP_FILES=1

# --- Rules ---
# Build the original payloads plus the new save-only extractor by default.
all: $(ELF_INFO) $(ELF_SEGMENTS) $(ELF_ALL) $(ELF_SAVEDATA) $(ELF_SAVE_EXTRACTOR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Original upstream payloads share main.c + bar_srv.c.
$(ELF_INFO) $(ELF_SEGMENTS) $(ELF_SAVEDATA) $(ELF_ALL): $(SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)

# New automatic PS4/PS5 save-only extractor.
$(ELF_SAVE_EXTRACTOR): $(SAVE_EXTRACTOR_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SAVE_EXTRACTOR_SRCS)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean
