include $(TOPDIR)/rules.mk

PKG_NAME:=wemo-base
PKG_VERSION:=1
PKG_RELEASE:=1
PKG_LICENSE:=Proprietary

include $(INCLUDE_DIR)/package.mk

define Package/wemo-base
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Wemo base files and init scripts
  DEPENDS:=+uci +busybox
endef

# No source to fetch/build
define Build/Prepare
endef
define Build/Configure
endef
define Build/Compile
endef

define Package/wemo-base/install
	# Create dirs
	$(INSTALL_DIR) \
		$(1)/etc/config \
		$(1)/etc/init.d \
		$(1)/etc/hotplug.d/iface \
		$(1)/etc/hotplug.d/button \
		$(1)/usr/bin \
		$(1)/usr/sbin

	# Configs (0644)
	$(INSTALL_DATA) ./files/etc/config/system   $(1)/etc/config/system
	$(INSTALL_DATA) ./files/etc/config/network  $(1)/etc/config/network
	$(INSTALL_DATA) ./files/etc/config/firewall $(1)/etc/config/firewall

	# Init scripts (0755)
	$(INSTALL_BIN)  ./files/etc/init.d/wemo-ssid                    $(1)/etc/init.d/wemo-ssid
	$(INSTALL_BIN)  ./files/etc/init.d/wemo-commissioning-watcher  $(1)/etc/init.d/wemo-commissioning-watcher
	$(INSTALL_BIN)  ./files/etc/init.d/wemo-migrate-overlay        $(1)/etc/init.d/wemo-migrate-overlay

	# Hotplug snippets (0755)
	$(INSTALL_BIN)  ./files/etc/hotplug.d/iface/30-wemo-ap-vendorie $(1)/etc/hotplug.d/iface/30-wemo-ap-vendorie
	$(INSTALL_BIN)  ./files/etc/hotplug.d/button/95-wemo-reset      $(1)/etc/hotplug.d/button/95-wemo-reset

	# Binaries / scripts (0755)
	$(INSTALL_BIN)  ./files/usr/bin/relay            $(1)/usr/bin/relay
	$(INSTALL_BIN)  ./files/usr/sbin/provision.sh    $(1)/usr/sbin/provision.sh
	$(INSTALL_BIN)  ./files/usr/sbin/wemo-commisioning-watcher $(1)/usr/sbin/wemo-commisioning-watcher
endef

define Package/wemo-base/conffiles
/etc/config/dhcp
/etc/config/firewall
/etc/config/network
/etc/config/system
/etc/config/wireless
/etc/config/matter
endef

# enable services on install (image or opkg)
define Package/wemo-base/postinst
#!/bin/sh
[ -n "$$IPKG_INSTROOT" ] || {
  /etc/init.d/wemo-ssid enable
  /etc/init.d/wemo-commissioning-watcher enable
  /etc/init.d/wemo-migrate-overlay enable
}
exit 0
endef

$(eval $(call BuildPackage,wemo-base))

