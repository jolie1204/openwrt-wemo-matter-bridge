include $(TOPDIR)/rules.mk

PKG_NAME:=wemo-rootfs-resize
PKG_VERSION:=1
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/wemo-rootfs-resize
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Wemo Raspberry Pi rootfs first-boot resize
  DEPENDS:=+sfdisk +partx-utils +resize2fs +findfs
endef

define Package/wemo-rootfs-resize/description
Expand Raspberry Pi ext4 root partitions to fill the backing SD/eMMC device on
first boot.
endef

define Build/Compile
endef

define Package/wemo-rootfs-resize/install
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/usr/libexec
	$(INSTALL_BIN) ./files/wemo-rootfs-resize.init $(1)/etc/init.d/wemo-rootfs-resize
	$(INSTALL_BIN) ./files/wemo-rootfs-resize $(1)/usr/libexec/wemo-rootfs-resize
endef

$(eval $(call BuildPackage,wemo-rootfs-resize))
