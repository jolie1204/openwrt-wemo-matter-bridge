include $(TOPDIR)/rules.mk

PKG_NAME:=matter-switch
PKG_RELEASE:=1

PKG_SOURCE_URL:=https://github.com/project-chip/connectedhomeip.git
PKG_SOURCE_PROTO:=git
PKG_SOURCE_MIRROR:=0
# Submodules needed for lighting-app
PKG_SOURCE_SUBMODULES:= \
  third_party/pigweed/repo \
  third_party/jsoncpp/repo \
  third_party/nlassert/repo \
  third_party/nlio/repo
# Pin to the same commit you’re using elsewhere
PKG_SOURCE_DATE:=2025-07-08
PKG_SOURCE_VERSION:=dda2252eb28440305c883d0204737dce5b3abe62
PKG_MIRROR_HASH:=ad81137221dafc0302c70cb90a03ab2472a02ee3c03e8cef8edcba26fd34201d
PKG_HASH:=b50bef90a40c56ef86a3b98cee06f6bd075824db00fe70d172774f2feac462d5
PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=LICENSE

# Host deps (same pattern as your netman)
HAVE_PY311:=$(shell $(STAGING_DIR_HOST)/bin/$(PYTHON) -c 'import sys; print(sys.version_info>=(3,11) or "")')
PKG_BUILD_DEPENDS:=gn/host $(if $(HAVE_PY311),,python3-host-ssl/host)
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/matter-switch/Default
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Matter
  TITLE:=Matter On/Off Switch (lighting-app based)
  URL:=https://project-chip.github.io/connectedhomeip-doc/
  DEPENDS:=+libstdcpp +libatomic +libpthread
endef

define Package/matter-switch-mbedtls
  $(call Package/matter-switch/Default)
  TITLE+= (mbedtls)
  DEPENDS+= +libmbedtls
  VARIANT:=mbedtls
  DEFAULT_VARIANT:=1
endef

define Package/matter-switch-openssl
  $(call Package/matter-switch/Default)
  TITLE+= (openssl)
  DEPENDS+= +libopenssl
  CONFLICTS:=matter-switch-mbedtls
  VARIANT:=openssl
endef

# Keep binaries lean and OpenWrt-native
CONFIGURE_OPTIONS := \
  --enable-recommended=no \
  --enable-access-restrictions=no \
  --enable-network-layer-ble=no \
  --enable-openthread=no \
  --enable-tracing-support=no \
  --enable-wifi=no

# Crypto choice
ifeq ($(BUILD_VARIANT),mbedtls)
  CONFIGURE_OPTIONS += --crypto=mbedtls --enable-external-mbedtls
  TARGET_LDFLAGS    += -lmbedcrypto -lmbedx509
else
  CONFIGURE_OPTIONS += --crypto=openssl
endif

# KVS/config paths (like your netman)
CONF_DIR:=/etc/matter
TARGET_CFLAGS += \
  -DCHIP_CONFIG_KVS_PATH=\"$(CONF_DIR)/chip_kvs.ini\" \
  -DFATCONFDIR=\"$(CONF_DIR)\" \
  -DSYSCONFDIR=\"$(CONF_DIR)\" \
  -DLOCALSTATEDIR=\"$(CONF_DIR)\" \
  -DCHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME=\"Wemo\" \
  -DCHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME=\"Mini\"

# Silence musl fortify nonliteral printf warnings (you hit this before)
TARGET_CFLAGS   += -Wno-format-nonliteral
TARGET_CXXFLAGS += -Wno-format-nonliteral

# Shared build env + output dir
CHIP_BUILD_ENV_DIR:=$(STAGING_DIR_HOST)/share/matter-build-env
OUT_DIR:=$(PKG_BUILD_DIR)/out/openwrt

# Where we’ll overlay your app sources
UPSTREAM_APP_DIR := $(PKG_BUILD_DIR)/examples/lighting-app/linux
LOCAL_SRC_DIR    := $(CURDIR)/src

LOCAL_APP_DIR    := $(LOCAL_SRC_DIR)/linux
LOCAL_COMMON_DIR := $(LOCAL_SRC_DIR)/wemo-switch-common
COMMON_DST_DIR   := $(PKG_BUILD_DIR)/examples/lighting-app/wemo-switch-common

define Build/Prepare
	# 1) fetch/extract SDK
	$(call Build/Prepare/Default)

	# 2) overlay your app + common into the CHIP tree
ifneq ($(wildcard $(LOCAL_SRC_DIR)),)
	# sanity checks
	[ -f $(LOCAL_APP_DIR)/main.cpp ] || { echo "ERROR: $(LOCAL_APP_DIR)/main.cpp not found"; exit 1; }
	[ -f $(LOCAL_COMMON_DIR)/BUILD.gn ] || { echo "ERROR: $(LOCAL_COMMON_DIR)/BUILD.gn not found"; exit 1; }

	# ensure destinations exist
	mkdir -p $(UPSTREAM_APP_DIR) $(COMMON_DST_DIR)

	# (A) overlay linux app sources into examples/lighting-app/linux
	#     exclude the local third_party redirect
	( cd $(LOCAL_APP_DIR) && tar --exclude='./third_party' -cf - . ) | \
	  ( cd $(UPSTREAM_APP_DIR) && tar -xf - )

	# (B) overlay common bits into examples/lighting-app/wemo-switch-common
	tar -C $(LOCAL_COMMON_DIR) -cf - . | tar -C $(COMMON_DST_DIR) -xf -

	# quick check for the ZAP (since you're using ZAP path)
	[ -f $(COMMON_DST_DIR)/lighting-app.zap ] || { echo "ERROR: lighting-app.zap not found in $(COMMON_DST_DIR)"; exit 1; }
endif
endef

define Build/Configure
	mkdir -p $(OUT_DIR) && cd $(OUT_DIR) && \
		$(CONFIGURE_VARS) $(PKG_BUILD_DIR)/scripts/configure \
			--build-env-dir="$(CHIP_BUILD_ENV_DIR)" \
			--project=examples/lighting-app/linux \
			--target=$(GNU_TARGET_NAME) \
			$(CONFIGURE_OPTIONS)
endef

define Build/Compile
	$(NINJA) -C $(OUT_DIR) chip-lighting-app
endef

define Package/matter-switch/Default/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d $(1)$(CONF_DIR) $(1)/usr/share/matter
	$(INSTALL_BIN) $(OUT_DIR)/chip-lighting-app $(1)/usr/sbin/matter-switchd
	$(INSTALL_BIN) ./files/matter-switch.init $(1)/etc/init.d/matter-switch
	$(INSTALL_DATA) ./files/bootstrap.sh $(1)/usr/share/matter/bootstrap.sh
endef

Package/matter-switch-mbedtls/install=$(Package/matter-switch/Default/install)
Package/matter-switch-openssl/install=$(Package/matter-switch/Default/install)

$(eval $(call BuildPackage,matter-switch-mbedtls))
$(eval $(call BuildPackage,matter-switch-openssl))
