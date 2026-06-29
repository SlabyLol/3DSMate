#---------------------------------------------------------------------------------
# 3DSMate
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error Please set DEVKITPRO)
endif

include $(DEVKITPRO)/3ds.mk

TARGET      := 3DSMate
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include

APP_TITLE       := 3DSMate
APP_DESCRIPTION := AI Chat powered by Groq
APP_AUTHOR      := 3DSMate Project
APP_ICON        := meta/icon.png

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := -O2 -Wall -g $(ARCH) -D__3DS__
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS  := $(ARCH)

LIBS := -ljansson \
        -lmbedtls \
        -lmbedx509 \
        -lmbedcrypto \
        -lctru \
        -lm

LIBDIRS := $(CTRULIB) \
           $(DEVKITPRO)/portlibs/3ds

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := \
$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
$(foreach dir,$(DATA),$(CURDIR)/$(dir))

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OFILES := \
$(CFILES:.c=.o) \
$(CPPFILES:.cpp=.o) \
$(SFILES:.s=.o) \
$(BINFILES:=.o)

export INCLUDE := \
$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
-I$(CURDIR)/$(BUILD)

export LIBPATHS := \
$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean

all: $(BUILD)

$(BUILD):
	@mkdir -p $(BUILD)
	@$(MAKE) -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	rm -rf $(BUILD)
	rm -f $(TARGET).3dsx
	rm -f $(TARGET).elf
	rm -f $(TARGET).smdh
	rm -f $(TARGET).cia

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
