# Copyright (c) 2023-2025 Project CHIP Authors
# SPDX-License-Identifier: Apache-2.0

include $(TOPDIR)/rules.mk

PKG_NAME:=matter-commissioner
PKG_RELEASE:=1

PKG_SOURCE_URL:=https://github.com/project-chip/connectedhomeip.git
PKG_SOURCE_PROTO:=git
PKG_SOURCE_MIRROR:=0
# Keep the same submodules pattern as matter-netman
PKG_SOURCE_SUBMODULES:=\
  third_party/pigweed/repo \
  third_party/jsoncpp/repo \
  third_party/nlassert/repo \
  third_party/nlio/repo \
  third_party/perfetto/repo \
  third_party/libwebsockets/repo \
  third_party/editline/repo \

# Use the same commit/date you pinned for matter-netman
PKG_SOURCE_DATE:=2025-07-08
PKG_SOURCE_VERSION:=dda2252eb28440305c883d0204737dce5b3abe62
# Mirror tarball hash (from the feed maintainer); leave as-is if you don't use mirrors
PKG_MIRROR_HASH:=ad81137221dafc0302c70cb90a03ab2472a02ee3c03e8cef8edcba26fd34201d
# Local tarball hash (fill after first download, like you did for netman)
PKG_HASH:=0bcad6ed72138268e4dfe26141304e476bbe30f5eefcb429d64a78bcb1155edd
PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=LICENSE

# Host tooling (same pattern as matter-netman)
HAVE_PYTHON_311:=$(shell $(STAGING_DIR_HOST)/bin/$(PYTHON) -c 'import sys; print(sys.version_info>=(3,11) or "")')
PKG_BUILD_DEPENDS:=gn/host $(if $(HAVE_PYTHON_311),,python3-host-ssl/host)
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/matter-commissioner/Default
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Matter
  TITLE:=Matter Commissioner (chip-tool)
  URL:=https://project-chip.github.io/connectedhomeip-doc/
  DEPENDS:=+libstdcpp +libatomic +libpthread
endef

define Package/matter-commissioner-mbedtls
  $(call Package/matter-commissioner/Default)
  TITLE+= (mbedtls)
  DEPENDS+= +libmbedtls
  VARIANT:=mbedtls
  DEFAULT_VARIANT:=1
endef

define Package/matter-commissioner-openssl
  $(call Package/matter-commissioner/Default)
  TITLE+= (openssl)
  DEPENDS+= +libopenssl
  CONFLICTS:=matter-commissioner-mbedtls
  VARIANT:=openssl
endef

# General configure options (similar to matter-netman)
CONFIGURE_VARS += CHIP_TRACING=0 CHIP_ENABLE_TRACING=0 CHIP_ENABLE_WSTUNNEL=0 chip_build_libwebsockets=0 chip_libwebsockets_build=0
CONFIGURE_OPTIONS:=\
  --enable-recommended=no \
  --enable-access-restrictions=no \
  --enable-wifi=no \
  --enable-openthread=no \
  --enable-network-layer-ble=no \
  --enable-tracing-support=no \
  --enable-wstunnel=no
TARGET_CFLAGS += -DCHIP_HAVE_TRACING=0

# Crypto backend
ifeq ($(BUILD_VARIANT),mbedtls)
CONFIGURE_OPTIONS+= --crypto=mbedtls --enable-external-mbedtls
TARGET_LDFLAGS+= -lmbedcrypto -lmbedx509
endif
ifeq ($(BUILD_VARIANT),openssl)
CONFIGURE_OPTIONS+= --crypto=openssl
endif

# https://github.com/openwrt/openwrt/issues/13016
TARGET_CFLAGS+= -Wno-format-nonliteral

# Shared build env/output dir
CHIP_BUILD_ENV_DIR:=$(STAGING_DIR_HOST)/share/matter-build-env
OUT_DIR:=$(PKG_BUILD_DIR)/out/openwrt

define Build/Configure
	mkdir -p $(OUT_DIR) && cd $(OUT_DIR) && \
		$(CONFIGURE_VARS) $(PKG_BUILD_DIR)/scripts/configure \
			--build-env-dir="$(CHIP_BUILD_ENV_DIR)" \
			--project=examples/chip-tool \
			--target=$(GNU_TARGET_NAME) \
			$(CONFIGURE_OPTIONS)
endef

define Build/Compile
	$(NINJA) -C $(OUT_DIR) chip-tool
endef

define Package/matter-commissioner/Default/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(OUT_DIR)/chip-tool $(1)/usr/bin/chip-tool
	ln -sf /usr/bin/chip-tool $(1)/usr/bin/matter-commissioner
endef

Package/matter-commissioner-mbedtls/install=$(Package/matter-commissioner/Default/install)
Package/matter-commissioner-openssl/install=$(Package/matter-commissioner/Default/install)

$(eval $(call BuildPackage,matter-commissioner-mbedtls))
$(eval $(call BuildPackage,matter-commissioner-openssl))
