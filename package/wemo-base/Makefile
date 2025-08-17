include $(TOPDIR)/rules.mk

PKG_NAME:=wemo-base
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/wemo-base
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Wemo base files and init scripts
  DEPENDS:=+uci +busybox
endef

define Package/wemo-base/install
	$(CP) ./files/* $(1)/
	# make sure init scripts are executable
	chmod 0755 $(1)/etc/init.d/wemo-ssid
	chmod 0755 $(1)/etc/init.d/wemo-commissioning-watcher
	chmod 0755 $(1)/etc/init.d/wemo-migrate-overlay
endef

# enable the init scripts on install/first boot
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

