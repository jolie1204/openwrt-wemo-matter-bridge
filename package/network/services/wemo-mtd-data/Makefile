include $(TOPDIR)/rules.mk

PKG_NAME:=wemo-mtd-data
PKG_VERSION:=1
PKG_RELEASE:=2

PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/wemo-mtd-data
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Wemo bridge MTD data mount
endef

define Package/wemo-mtd-data/description
Mount the Wemo bridge MTD partition named data at /data and prepare persistent
state directories for bridge services.
endef

define Build/Compile
endef

define Package/wemo-mtd-data/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/wemo_mtd_data.init $(1)/etc/init.d/wemo_mtd_data
endef

$(eval $(call BuildPackage,wemo-mtd-data))
