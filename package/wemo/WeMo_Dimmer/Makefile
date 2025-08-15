include $(TOPDIR)/rules.mk

PKG_NAME:=WeMo_Dimmer
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/belkin.mk

export WEMO_APP
export OUTPUT := wemoApp
export OBJS := $(patsubst %.c,%.o,$(notdir $(wildcard src/*.cpp) $(wildcard src/*.c)))
export VPATH := $(CURDIR)/src

TARGET_CFLAGS +=  -I$(STAGING_DIR)/usr/include/upnp
TARGET_LDFLAGS += -lbelkin_nvram -lbelkin_api -L$(STAGING_DIR)/usr/lib -lz
TARGET_LDFLAGS += -lAddlInfra
TARGET_LDFLAGS += -lUPnPHndlr
TARGET_LDFLAGS += -lupnp -lixml -lpthread -lssl -lcrypto
TARGET_LDFLAGS += -lcurl -lcares -lm
TARGET_LDFLAGS += -lmxml -lsqlite3
TARGET_LDFLAGS += -lWasp -lhkstore

define Package/WeMo_Dimmer
  CATEGORY := Belkin
  SUBMENU  := Apps
  SECTION  := utils
  TITLE    := WeMo Dimmer App
  DEPENDS  += +UPnP +libpthread +libcares +libupnp +libuuid
  DEPENDS  += +belkin_nvram_bd +HomeKitStore
endef

define Package/WeMo_Dimmer/description
	WeMo Dimmer primary application
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	echo "$$$$WEMO_APP" > $(PKG_BUILD_DIR)/Makefile
endef

define Build/Compile
	$(MAKE_VARS) $(MAKE) -C $(PKG_BUILD_DIR)/$(MAKE_PATH) $(MAKE_FLAGS)
	@echo $(shell date "+%x %X") > $(PKG_BUILD_DIR)/ver.txt
ifeq ($(CONFIG_WEMO_PRODUCTION_IMAGE),)
	@echo WeMo_WW_2.00.$(shell $(TOPDIR)/scripts/revision.sh).DVT-OWRT-Dimmer >> $(PKG_BUILD_DIR)/ver.txt
else
 ifeq ($(CONFIG_DEFAULT_dropbear),)
	@echo WeMo_WW_2.00.$(shell $(TOPDIR)/scripts/revision.sh).PVT-OWRT-Dimmer >> $(PKG_BUILD_DIR)/ver.txt
 else
	@echo WeMo_WW_2.00.$(shell $(TOPDIR)/scripts/revision.sh).BVT-OWRT-Dimmer >> $(PKG_BUILD_DIR)/ver.txt
 endif
endif
endef

define Package/WeMo_Dimmer/install
	$(INSTALL_DIR) $(1)/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/$(OUTPUT) $(1)/sbin/
	$(INSTALL_BIN) ./files/startWemo.sh $(1)/sbin/
	$(INSTALL_BIN) ./files/identify_hw.sh $(1)/sbin/
ifeq ($(CONFIG_WEMO_PRODUCTION_IMAGE),)
	$(INSTALL_BIN) ./files/test_setup.sh $(1)/sbin/
endif
ifeq ($(CONFIG_TARGET_x86),y)
	$(INSTALL_BIN) ./files/config_dimmer.sh $(1)/sbin/
endif
	$(INSTALL_DIR) $(1)/sbin/web/
	$(INSTALL_BIN) ./target/eventservice.xml $(1)/sbin/web/.
	$(INSTALL_BIN) ./target/Dimmersetup.xml $(1)/sbin/web/setup.xml
	$(INSTALL_DIR) $(1)/etc/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/wemo.init $(1)/etc/init.d/wemo
ifeq ($(CONFIG_TARGET_x86),y)
	$(INSTALL_BIN) ./files/wemo.x86 $(1)/etc/init.d/wemo
	$(INSTALL_DIR) $(1)/etc/
	$(INSTALL_BIN) ./files/nvram.bin $(1)/etc/
endif
	$(INSTALL_BIN) target/Dimmer.png $(1)/etc/icon.jpg
	$(CP) $(PKG_BUILD_DIR)/ver.txt $(1)/etc
endef

$(eval $(call BuildPackage,WeMo_Dimmer))
