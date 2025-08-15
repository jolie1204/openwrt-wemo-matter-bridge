include $(TOPDIR)/rules.mk

PKG_NAME:=WeMo_SNSV2
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
TARGET_LDFLAGS += -lhkstore

define Package/WeMo_SNSV2
  CATEGORY := Belkin
  SUBMENU  := Apps
  SECTION  := utils
  TITLE    := WeMo SNS V2 App
  DEPENDS  += +UPnP +libpthread +libcares +libupnp +libuuid +sqlite
  DEPENDS  += +HomeKitStore +belkin_nvram_bd
endef

define Package/WeMo_SNSV2/description
	WeMo SNS version 2 primary application
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	echo "$$$$WEMO_APP" > $(PKG_BUILD_DIR)/Makefile
endef

define Build/Compile
	$(MAKE_VARS) $(MAKE) -C $(PKG_BUILD_DIR)/$(MAKE_PATH) $(MAKE_FLAGS)
	@echo $(shell date "+%x %X") > $(PKG_BUILD_DIR)/ver.txt
ifeq ($(CONFIG_WEMO_PRODUCTION_IMAGE),)
	@echo WeMo_WW_2.00.$(shell $(TOPDIR)/scripts/revision.sh).DVT-OWRT-SNSV2 >> $(PKG_BUILD_DIR)/ver.txt
else
 ifeq ($(CONFIG_DEFAULT_dropbear),)
	@echo WeMo_WW_2.00.$(shell $(TOPDIR)/scripts/revision.sh).PVT-OWRT-SNSV2 >> $(PKG_BUILD_DIR)/ver.txt
 else
	@echo WeMo_WW_2.00.$(shell $(TOPDIR)/scripts/revision.sh).BVT-OWRT-SNSV2 >> $(PKG_BUILD_DIR)/ver.txt
 endif
endif
endef

define Package/WeMo_SNSV2/install
	$(INSTALL_DIR) $(1)/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/$(OUTPUT) $(1)/sbin/
	$(INSTALL_BIN) ./files/startWemo.sh $(1)/sbin/
	$(INSTALL_BIN) ./files/Test_GPIO.sh $(1)/sbin/
	$(INSTALL_BIN) ./files/Demo_GPIO.sh $(1)/sbin/
	$(INSTALL_BIN) ./files/factoryreset $(1)/sbin/
	$(INSTALL_BIN) ./files/resetwifi	$(1)/sbin/
	$(INSTALL_BIN) ./files/triggerswitch $(1)/sbin/
	$(INSTALL_BIN) ./files/identify_hw.sh $(1)/sbin/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/wemo.init $(1)/etc/init.d/wemo
ifeq ($(CONFIG_TARGET_x86),y)
	$(INSTALL_BIN) ./files/wemo.x86 $(1)/etc/init.d/wemo
	$(INSTALL_DIR) $(1)/etc/
	$(INSTALL_BIN) ./files/nvram.bin $(1)/etc/
endif
	$(INSTALL_DIR) $(1)/sbin/web/
	$(INSTALL_BIN) ./target/setup.xml $(1)/sbin/web/setup.xml
	$(INSTALL_BIN) ./target/eventservice.xml $(1)/sbin/web/eventservice.xml
	$(CP) target/icon.jpg $(1)/etc/
	$(CP) target/sensor.jpg $(1)/etc/
	$(CP) $(PKG_BUILD_DIR)/ver.txt $(1)/etc
endef

$(eval $(call BuildPackage,WeMo_SNSV2))
