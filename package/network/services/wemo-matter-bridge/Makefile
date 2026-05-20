include $(TOPDIR)/rules.mk

PKG_NAME:=wemo-matter-bridge
PKG_RELEASE:=12

PKG_SOURCE_URL:=https://github.com/jolie1204/connectedhomeip.git
PKG_SOURCE_PROTO:=git-with-metadata
PKG_SOURCE_MIRROR:=0
PKG_SOURCE_SUBMODULES:= \
  third_party/pigweed/repo \
  third_party/jsoncpp/repo \
  third_party/nlassert/repo \
  third_party/nlio/repo
PKG_SOURCE_DATE:=2025-12-01
PKG_SOURCE_VERSION:=8effa808dd9fa195ec0294f0ad67c80a86dd4975
PKG_MIRROR_HASH:=skip

PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=LICENSE

HAVE_PY311:=$(shell $(STAGING_DIR_HOST)/bin/$(PYTHON) -c 'import sys; print(sys.version_info>=(3,11) or "")')
PKG_BUILD_DEPENDS:=gn/host $(if $(HAVE_PY311),,python3-host-ssl/host)
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/git-with-metadata.mk

define Package/wemo-matter-bridge
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Matter
  TITLE:=WeMo Matter bridge
  URL:=https://github.com/jolie1204/openwrt-wemo-matter-bridge
  DEPENDS:=+libstdcpp +libatomic +libpthread +libmbedtls +libsqlite3 +libupnp +openwemo-bridge-core +wemo-mtd-data
endef

define Package/wemo-matter-bridge/description
Matter bridge application that exposes discovered WeMo devices as bridged Matter
endpoints.
endef

define Package/wemo-matter-bridge/conffiles
/data/wemo-matter/
endef

CONFIGURE_OPTIONS:= \
	--enable-recommended=no \
	--enable-access-restrictions=no \
	--enable-network-layer-ble=no \
	--enable-openthread=no \
	--enable-tracing-support=no \
	--enable-wifi=no \
	--crypto=mbedtls \
	--enable-external-mbedtls

TARGET_CFLAGS += \
	-DCHIP_CONFIG_KVS_PATH=\"/data/wemo-matter/chip/chip_kvs\" \
	-DFATCONFDIR=\"/data/wemo-matter/chip\" \
	-DSYSCONFDIR=\"/data/wemo-matter/chip\" \
	-DLOCALSTATEDIR=\"/data/wemo-matter/chip\" \
	-DCHIP_DEVICE_CONFIG_DEVICE_TYPE=0x000E \
	-DCHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME=\"Wemo\" \
	-DCHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME=\"Matter_Bridge\" \
	-Wno-format-nonliteral

TARGET_CXXFLAGS += -Wno-format-nonliteral
TARGET_LDFLAGS += -lmbedtls -lmbedcrypto -lmbedx509

CHIP_BUILD_ENV_DIR:=$(STAGING_DIR_HOST)/share/matter-build-env
OUT_DIR:=$(PKG_BUILD_DIR)/out/openwrt
APP_DIR:=$(PKG_BUILD_DIR)/examples/wemo-bridge-app

define Build/Prepare
	$(call Build/Prepare/Default)
	rm -rf $(APP_DIR)
	mkdir -p $(APP_DIR)/linux
	$(CP) ./app-src/matter-bridge-app/. $(APP_DIR)/linux/
	$(CP) ./app-src/include $(APP_DIR)/
	$(CP) ./app-src/src $(APP_DIR)/
	rm -f $(APP_DIR)/linux/connectedhomeip $(APP_DIR)/linux/third_party
	ln -s ../../.. $(APP_DIR)/linux/connectedhomeip
	ln -s connectedhomeip/third_party $(APP_DIR)/linux/third_party
	$(INSTALL_DATA) ./files/BUILD.gn $(APP_DIR)/linux/BUILD.gn
	$(INSTALL_DATA) ./files/args.gni $(APP_DIR)/linux/args.gni
	$(INSTALL_DATA) ./files/build.gni $(APP_DIR)/linux/build_overrides/build.gni
	$(INSTALL_DATA) ./files/chip.gni $(APP_DIR)/linux/build_overrides/chip.gni
	$(INSTALL_DATA) ./files/jsoncpp.gni $(APP_DIR)/linux/build_overrides/jsoncpp.gni
endef

define Build/Configure
	mkdir -p $(OUT_DIR) && cd $(OUT_DIR) && \
		OPENWEMO_ENGINE_INCLUDE_DIR="$(STAGING_DIR)/usr/include/openwemo" \
		OPENWEMO_ENGINE_LIB_DIR="$(STAGING_DIR)/usr/lib" \
		WEMO_CHIP_STATE_DIR="/data/wemo-matter/chip" \
		$(CONFIGURE_VARS) $(PKG_BUILD_DIR)/scripts/configure \
			--build-env-dir="$(CHIP_BUILD_ENV_DIR)" \
			--project=examples/wemo-bridge-app/linux \
			--target=$(GNU_TARGET_NAME) \
			$(CONFIGURE_OPTIONS)
endef

define Build/Compile
	$(NINJA) -C $(OUT_DIR) wemo-bridge-app
endef

define Package/wemo-matter-bridge/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/libexec $(1)/etc/init.d $(1)/etc/rc.button
	$(INSTALL_BIN) $(OUT_DIR)/wemo-bridge-app $(1)/usr/sbin/wemo-bridge-app
	$(INSTALL_BIN) ./files/wemo-matter-bridge.init $(1)/etc/init.d/wemo-matter-bridge
	$(INSTALL_BIN) ./files/wemo-indicator $(1)/usr/libexec/wemo-indicator
	$(INSTALL_BIN) ./files/wemo-indicator.init $(1)/etc/init.d/wemo-indicator
	$(INSTALL_BIN) ./files/button $(1)/etc/rc.button/button
endef

$(eval $(call BuildPackage,wemo-matter-bridge))
