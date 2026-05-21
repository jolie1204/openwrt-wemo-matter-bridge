include $(TOPDIR)/rules.mk

PKG_NAME:=openwemo-bridge-core
PKG_VERSION:=0.1
PKG_RELEASE:=15

PKG_LICENSE:=BSD-3-Clause
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/openwemo-bridge-core
  SECTION:=net
  CATEGORY:=Network
  TITLE:=OpenWeMo control daemon and engine
  URL:=https://github.com/jolie1204/openwrt-wemo-matter-bridge
  DEPENDS:=+libatomic +libpthread +libsqlite3 +libupnp +libopenssl +sqlite3-cli +wemo-mtd-data
endef

define Package/openwemo-bridge-core/description
OpenWeMo UPnP control daemon and shared engine library used by the Wemo Matter
bridge.
endef

define Package/openwemo-bridge-core/conffiles
/etc/wemo_ctrl.conf
/data/wemo-matter/
endef

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
	$(INSTALL_DATA) ./files/atomic_compat.h $(PKG_BUILD_DIR)/atomic_compat.h
	$(INSTALL_DATA) ./files/ithread.h $(PKG_BUILD_DIR)/wemo_ctrl/ithread.h
	$(Build/Patch)
endef

define Build/Configure
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/wemo_engine \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -fPIC -include $(PKG_BUILD_DIR)/atomic_compat.h" \
		LFLAGS="-pthread -shared $(TARGET_LDFLAGS)" \
		INCLUDES="-I$(STAGING_DIR)/usr/include -I$(STAGING_DIR)/usr/include/upnp" \
		LIBS="-lsqlite3 -latomic"
	$(MAKE) -C $(PKG_BUILD_DIR)/wemo_ctrl \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -include $(PKG_BUILD_DIR)/atomic_compat.h" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		LFLAGS="-pthread" \
		RPATH_FLAGS= \
		INCLUDES="-I../wemo_engine -I$(STAGING_DIR)/usr/include -I$(STAGING_DIR)/usr/include/upnp" \
		LIBS="../wemo_engine/libwemoengine.a -lupnp -lixml -lsqlite3 -latomic"
	$(MAKE) -C $(PKG_BUILD_DIR)/wemo_client \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -include $(PKG_BUILD_DIR)/atomic_compat.h" \
		LDFLAGS="$(TARGET_LDFLAGS) -pthread" \
		RPATH_FLAGS= \
		INCLUDES="-I../wemo_engine -I$(STAGING_DIR)/usr/include -I$(STAGING_DIR)/usr/include/upnp" \
		LIBS="../wemo_engine/libwemoengine.a -lupnp -lixml -lsqlite3 $(STAGING_DIR)/usr/lib/libcrypto.a -latomic -lz"
endef

define Package/openwemo-bridge-core/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/lib $(1)/etc/init.d $(1)/etc
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wemo_ctrl/wemo_ctrl $(1)/usr/sbin/wemo_ctrl
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wemo_client/wemo_client $(1)/usr/sbin/wemo_client
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wemo_engine/libwemoengine.so $(1)/usr/lib/libwemoengine.so
	$(INSTALL_BIN) ./files/wemo_ctrl.init $(1)/etc/init.d/wemo_ctrl
	$(INSTALL_CONF) ./files/wemo_ctrl.conf $(1)/etc/wemo_ctrl.conf
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include/openwemo $(1)/usr/lib
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/wemo_engine/wemo_engine.h $(1)/usr/include/openwemo/wemo_engine.h
	$(CP) $(PKG_BUILD_DIR)/wemo_engine/libwemoengine.{a,so} $(1)/usr/lib/
endef

$(eval $(call BuildPackage,openwemo-bridge-core))
