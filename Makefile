# SPDX-License-Identifier: GPL-2.0-or-later
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
ifeq ($(strip $(MAKECMDGOALS)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif
ifneq ($(strip $(filter-out test dist-test,$(MAKECMDGOALS))),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif
else
include $(DEVKITPRO)/libnx/switch_rules
endif

TOPDIR ?= $(CURDIR)

#---------------------------------------------------------------------------------
# SWOTS — persistent motion-sickness-prevention sysmodule.
# The presence of swots.json (matched by TARGET name) makes libnx build an
# ExeFS PFS0 (.nsp) for sysmodule deployment instead of a homebrew .nro.
#---------------------------------------------------------------------------------
TARGET       := swots
BUILD        := build
SOURCES      := source
DATA         := data
INCLUDES     := include
APP_VERSION  := $(strip $(file <$(TOPDIR)/VERSION))
APP_TITLE    := SWOTS
DIST_DIR     := dist
HOST_CXX     ?= g++
HOST_BUILD   := build-host
HOST_TESTS   := $(HOST_BUILD)/tesla_coexistence_test \
                $(HOST_BUILD)/settings_format_test \
                $(HOST_BUILD)/pixel_math_test \
                $(HOST_BUILD)/motion_math_test

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -Wextra -O2 -ffunction-sections \
           -Wno-missing-field-initializers $(ARCH) $(DEFINES)

CFLAGS  += $(INCLUDE) -D__SWITCH__ -DAPP_VERSION=\"$(APP_VERSION)\"

CXXFLAGS := $(CFLAGS) -std=c++20 -fno-exceptions

ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-wrap,exit -Wl,-Map,$(notdir $*.map)

LIBS    := -lnx

#---------------------------------------------------------------------------------
# list of directories containing libraries
#---------------------------------------------------------------------------------
LIBDIRS := $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES  := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES  := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all demo overlay setup-libtesla test verify dist dist-test

#---------------------------------------------------------------------------------
all: $(BUILD)

demo: $(BUILD) overlay

test: $(HOST_TESTS)
	@$(foreach test,$(HOST_TESTS),$(test) &&) true
	@python3 scripts/verify_metadata.py

$(HOST_BUILD)/tesla_coexistence_test: tests/tesla_coexistence_test.cpp \
                                      include/tesla_coexistence.hpp \
                                      include/tesla_exit_intent.hpp \
                                      include/tesla_lifecycle.hpp
	@mkdir -p $(HOST_BUILD)
	@$(HOST_CXX) -std=c++20 -O2 -Wall -Wextra -Werror -pedantic \
		-I$(CURDIR)/include $< -o $@

$(HOST_BUILD)/settings_format_test: tests/settings_format_test.cpp include/settings_format.hpp
	@mkdir -p $(HOST_BUILD)
	@$(HOST_CXX) -std=c++20 -O2 -Wall -Wextra -Werror -pedantic \
		-I$(CURDIR)/include $< -o $@

$(HOST_BUILD)/pixel_math_test: tests/pixel_math_test.cpp include/pixel_math.hpp
	@mkdir -p $(HOST_BUILD)
	@$(HOST_CXX) -std=c++20 -O2 -Wall -Wextra -Werror -pedantic \
		-I$(CURDIR)/include $< -o $@

$(HOST_BUILD)/motion_math_test: tests/motion_math_test.cpp include/motion_math.hpp
	@mkdir -p $(HOST_BUILD)
	@$(HOST_CXX) -std=c++20 -O2 -Wall -Wextra -Werror -pedantic \
		-I$(CURDIR)/include $< -o $@

verify: demo test
	@test -s $(TARGET).nsp
	@test -s tesla-overlay/$(TARGET).ovl
	@git -C libs/libtesla apply --reverse --check \
		$(CURDIR)/patches/libtesla-post-foreground-release.patch
	@python3 scripts/verify_npdm.py $(TARGET).npdm $(TARGET).nsp
	@echo "verified: $(TARGET).nsp and tesla-overlay/$(TARGET).ovl"

setup-libtesla:
	@./scripts/setup_libtesla.sh

dist: verify
	@python3 scripts/build_release.py \
		--version "$(APP_VERSION)" \
		--sysmodule "$(TARGET).nsp" \
		--overlay "tesla-overlay/$(TARGET).ovl" \
		--output-dir "$(DIST_DIR)"
	@python3 scripts/verify_release.py "$(DIST_DIR)/SWOTS-v$(APP_VERSION).zip"

dist-test:
	@python3 scripts/verify_release.py --self-test

overlay:
	@$(MAKE) --no-print-directory -C tesla-overlay \
		LIBTESLA=$(CURDIR)/libs/libtesla APP_VERSION=$(APP_VERSION)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(HOST_BUILD)
ifeq ($(strip $(APP_JSON)),)
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
	@rm -fr $(BUILD) $(TARGET).nsp $(TARGET).nso $(TARGET).npdm $(TARGET).elf
endif
	@$(MAKE) --no-print-directory -C tesla-overlay clean LIBTESLA=$(CURDIR)/libs/libtesla 2>/dev/null || true

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets — .nsp (sysmodule) when APP_JSON is set, else .nro (homebrew)
#---------------------------------------------------------------------------------
ifeq ($(strip $(APP_JSON)),)

all : $(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro : $(OUTPUT).elf
endif

else

all : $(OUTPUT).nsp

$(OUTPUT).nsp : $(OUTPUT).nso $(OUTPUT).npdm

$(OUTPUT).nso : $(OUTPUT).elf

endif

$(OUTPUT).elf : $(OFILES)

$(OFILES_SRC) : $(HFILES_BIN)

#---------------------------------------------------------------------------------
-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
