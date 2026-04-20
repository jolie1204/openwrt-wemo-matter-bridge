include $(TOPDIR)/rules.mk

PKG_NAME:=wemo-board-utils
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/wemo-board-utils
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Board-specific Wemo utilities
endef

define Package/wemo-board-utils/description
Board-specific helper scripts for Wemo mt7628 targets.
endef

define Build/Compile
endef

define Package/wemo-board-utils/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/program-eth-mac.sh $(1)/usr/sbin/program-eth-mac.sh
endef

$(eval $(call BuildPackage,wemo-board-utils))
