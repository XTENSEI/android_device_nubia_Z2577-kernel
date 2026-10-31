# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_POWER_RESET)	+= reset/
obj-$(CONFIG_POWER_SUPPLY)	+= supply/
ifdef CONFIG_VENDOR_SQC_CHARGER_SPRD
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/power
subdir-ccflags-y += -I$(srctree)/drivers/vendor/common/sqc_sprd
endif