include $(TOPDIR)/rules.mk

PKG_NAME:=wemohk
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/belkin.mk

ifeq ($(CONFIG_WEMO_PRODUCTION_IMAGE),)
TARGET_CFLAGS += -g
TARGET_CFLAGS += -DDEBUG_ENABLE
endif

TARGET_CFLAGS += -I$(STAGING_DIR)/include/ -I$(STAGING_DIR)/usr/include/upnp
TARGET_LDFLAGS += -L$(STAGING_DIR)/usr/lib/

define Package/wemohk
  CATEGORY	:= Belkin
  SUBMENU	:= WemoEngine
  SECTION	:= utils
  TITLE		:= Wemo Engine
  DEPENDS	:= +libupnp +sqlite +libopenssl +belkin_nvram_bd
endef

define Package/wemohk/description
	Wemo Engine for HomeKit
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) -a ./src/* $(PKG_BUILD_DIR)/.
endef

define Build/Configure
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		ROOTBINDIR="$(PKG_BUILD_DIR)" \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -Wall " \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_BUILD_DIR)/wemo_engine/wemo_engine.h $(1)/usr/include/
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/wemo_engine/libwemoengine.so $(1)/usr/lib/
endef

define Package/wemohk/install
	$(INSTALL_DIR) $(1)/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wemo_ctrl/wemo_ctrl $(1)/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wemo_client/wemo_client $(1)/sbin/
	$(INSTALL_DIR) $(1)/lib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wemo_engine/libwemoengine.so $(1)/lib
endef

$(eval $(call BuildPackage,wemohk))
