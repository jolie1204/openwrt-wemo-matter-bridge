include $(TOPDIR)/rules.mk

PKG_NAME := WeMo_Core
PKG_RELEASE := 1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/belkin_diag.mk

define Package/WeMo_Core
	CATEGORY := Belkin
	SUBMENU  := Libraries
	SECTION  := libs
	TITLE    := WeMo_Core
	$(call BELKIN_DIAG_DEPENDS)
endef

define Package/WeMo_Core/description
	Wemo_Core include files needed for WeMo_Core libraries
endef

define Package/WeMo_Core/config
config WEMO_PRODUCTION_IMAGE
	bool "Build Production WeMo image"
	default n
	help
	  This disables debugging symbols and populates root file
          system with password and inittab files to discourage console
          and ssh access.

endef

# Definitions adapted from .../openwrt/openwrt/common.mk
VER_MAJOR := 2
VER_MINOR := 00
VER_PATCH := 0
ifeq ($(CONFIG_WEMO_PRODUCTION_IMAGE),)
 ifeq ($(CONFIG_PACKAGE_WeMo_NetCam),y)
PLUGIN_BOARD_TYPE := DVT
 else
  ifeq ($(CONFIG_PACKAGE_WeMo_Insight),)
PLUGIN_BOARD_TYPE := DVT-OWRT
  else
PLUGIN_BOARD_TYPE := EVT-OWRT
  endif
 endif
else
 ifeq ($(CONFIG_PACKAGE_WeMo_NetCam),y)
PLUGIN_BOARD_TYPE := PVT
 else
  ifeq ($(CONFIG_DEFAULT_dropbear),)
PLUGIN_BOARD_TYPE := PVT-OWRT
  else
PLUGIN_BOARD_TYPE := BVT-OWRT
  endif
 endif
endif

ifeq ($(CONFIG_PACKAGE_WeMo_SNS),y)
PLUGIN_MODEL_NAME := SNS
endif

ifeq ($(CONFIG_PACKAGE_WeMo_Light),y)
PLUGIN_MODEL_NAME := LS
endif

ifeq ($(CONFIG_PACKAGE_WeMo_LightV2),y)
PLUGIN_MODEL_NAME := LightV2
endif

ifeq ($(CONFIG_PACKAGE_WeMo_Dimmer),y)
PLUGIN_MODEL_NAME := Dimmer
endif

ifeq ($(CONFIG_PACKAGE_WeMo_SNSV2),y)
PLUGIN_MODEL_NAME := SNSV2
endif

ifeq ($(CONFIG_PACKAGE_WeMo_SNSV3),y)
PLUGIN_MODEL_NAME := SNSV3
endif

ifeq ($(CONFIG_PACKAGE_WeMo_Insight),y)
ifeq ($(CONFIG_TARGET_rt288x_WemoInsightCR),)
PLUGIN_MODEL_NAME := Insight
else
PLUGIN_MODEL_NAME := InsightV2
endif
endif

ifeq ($(CONFIG_PACKAGE_WeMo_Link),y)
PLUGIN_MODEL_NAME := Link
endif

ifeq ($(CONFIG_PACKAGE_WeMo_Dimmer),y)
PLUGIN_MODEL_NAME := Dimmer
endif

ifeq ($(CONFIG_PACKAGE_WeMo_NetCam),y)
FWPREFIX := WeMo_NetCam
else
FWPREFIX := WeMo
endif

COUNTRY := $(if $(BUILD_TYPE),US,WW)
VERSION_FILE := $(STAGING_DIR)/etc/fw_version
FW_REV_H := $(STAGING_DIR)/include/fw_rev.h
BUILD_TIME := $(shell date "+%x %X")
FW_REV  := $(shell $(TOPDIR)/scripts/revision.sh)
FW_VERSION1 := $(VER_MAJOR).$(VER_MINOR)
ifeq ($(CONFIG_PACKAGE_WeMo_NetCam),y)
FW_VERSION  := $(FW_VERSION1).$(FW_REV).$(PLUGIN_BOARD_TYPE)
else
FW_VERSION  := $(FW_VERSION1).$(FW_REV).$(PLUGIN_BOARD_TYPE)-$(PLUGIN_MODEL_NAME)
endif

define Build/Prepare
	@/bin/echo Prepare WeMo_Core
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) Include/*.h $(STAGING_DIR)/include
	# From openwrt/openwrt/Makfile:
	-mkdir -p $(STAGING_DIR)/etc
	echo "Version: $(FWPREFIX)_$(COUNTRY)_$(FW_VERSION)" > $(VERSION_FILE)
	echo "Built: $(BUILD_TIME)" >> $(VERSION_FILE)
	echo "#ifndef __FW__REV__H__" > $(FW_REV_H)
	echo "#define __FW__REV__H__" >> $(FW_REV_H)
	echo "#define BUILD_TIME \"$(BUILD_TIME)\"" >> $(FW_REV_H)
	echo "#define FW_REV \"$(FWPREFIX)_$(COUNTRY)_$(FW_VERSION)\"" >> $(FW_REV_H)
	echo "#define FW_REV1 \"$(FW_VERSION1).$(FW_REV)\"" >> $(FW_REV_H)
	echo "#endif" >> $(FW_REV_H)
	echo "" >> $(FW_REV_H)
	chmod 644 $(FW_REV_H)
endef

define Build/Compile
	@/bin/echo Compile WeMo_Core
endef

define Build/InstallDev
	$(CP) -fa Include/*.h $(STAGING_DIR)/include
#	$(CP) -fa `find ../AddlInfra/src/ -name "*.h" -not -name config.h` $(STAGING_DIR)/include
ifeq ($(CONFIG_PACKAGE_WeMo_Link),y)
	$(CP) -fa ../subdevice/src/{subdevice,sd_configuration,sd_tracer}.h $(STAGING_DIR)/include
endif
ifeq ($(CONFIG_PACKAGE_WeMo_Insight),)
else
	$(CP) -fa ../HalInsight/src/insight.h $(STAGING_DIR)/include
	$(CP) -fa ../WeMo_Insight/src/InsightHandler.h $(STAGING_DIR)/include
endif
endef

define Package/WeMo_Core/install
	$(INSTALL_DIR) $(1)/etc
# Copy firmware env and nvram information
ifeq ($(CONFIG_PACKAGE_WeMo_Link),)
ifeq ($(CONFIG_TARGET_rt288x_WemoInsightCR),)
# SNS, LS, NetCam, Insight, Dimmer
	$(CP) files/fw_env_FLASH_OPTION_1.config  $(1)/etc/fw_env.config
	$(CP) files/nvram_FLASH_OPTION_1.config  $(1)/etc/nvram.config
else
# InsightCR
	$(CP) files/fw_env_FLASH_OPTION_2.config  $(1)/etc/fw_env.config
	$(CP) files/nvram_FLASH_OPTION_2.config  $(1)/etc/nvram.config
endif
else
# Link
	$(CP) files/fw_env_FLASH_OPTION_2.config  $(1)/etc/fw_env.config
	$(CP) files/nvram_FLASH_OPTION_2.config  $(1)/etc/nvram.config
endif
ifeq ($(CONFIG_PACKAGE_WeMo_SNSV2),) || ($(CONFIG_PACKAGE_WeMo_Dimmer),)
ifeq ($(CONFIG_WEMO_PRODUCTION_IMAGE),)
	$(CP) files/passwd_DVT  $(1)/etc/passwd
ifeq ($(CONFIG_PACKAGE_WeMo_Insight),)
ifeq ($(CONFIG_TARGET_x86),y)
	$(CP) files/inittab_x86 $(1)/etc/inittab
else
	$(CP) files/inittab_DVT $(1)/etc/inittab
endif
else
	$(CP) files/inittab_Insight_DVT $(1)/etc/inittab
endif
else # production image
 ifeq ($(CONFIG_DEFAULT_dropbear),)
	$(CP) files/passwd_PVT  $(1)/etc/passwd
 else
	$(CP) files/passwd_DVT  $(1)/etc/passwd
 endif
ifeq ($(CONFIG_PACKAGE_WeMo_Insight),)
	$(CP) files/inittab_PVT $(1)/etc/inittab
else
	$(CP) files/inittab_Insight_PVT $(1)/etc/inittab
endif
endif # ifeq ($(CONFIG_WEMO_PRODUCTION_IMAGE),)
endif # ifeq ($(CONFIG_PACKAGE_WeMo_SNSV2),) || ($(CONFIG_PACKAGE_WeMo_Dimmer),)
	$(INSTALL_DIR) $(1)/etc/certs
	$(CP) files/BuiltinObjectToken-GoDaddyClass2CA.crt $(1)/etc/certs/
	$(CP) files/devices.txt $(1)/etc/.
	$(INSTALL_DIR) $(1)/sbin
ifeq ($(CONFIG_WEMO_PRODUCTION_IMAGE),)
	$(CP) files/list_threads.sh $(1)/sbin
	chmod 777 $(1)/sbin/list_threads.sh
endif
endef

include $(INCLUDE_DIR)/belkin.mk

$(eval $(call BuildPackage,WeMo_Core))
