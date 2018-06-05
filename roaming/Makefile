CONFIG_DEBUG:=1
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

# Name and release number of this package
PKG_NAME:=roaming
PKG_RELEASE:=1
PKG_INSTALL:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)
KPKG_BUILD_DIR := $(PKG_BUILD_DIR)/kmod_roaming
#PKG_INSTALL_DIR := $(PKG_BUILD_DIR)/install
include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)/Default
	SECTION:=utils
	CATEGORY:=Utilities
	DEPENDS:=+libnl-tiny +libev
	TITLE:=zyc roaming
endef

define Package/$(PKG_NAME)
	$(call Package/$(PKG_NAME)/Default)
	TITLE+= server
endef

define Package/$(PKG_NAME)/description
	roaming server, maintenance roaming rules for clients
endef

define KernelPackage/zyc_roaming
	SECTION:=kernel
	CATEGORY:=Kernel modules
	SUBMENU:=Network Support
	DEPENDS:=+kmod-ipv6
	TITLE:=Kernel driver for ZYC ROAMING
	FILES:=$(KPKG_BUILD_DIR)/zyc_roaming.ko
	KCONFIG:=CONFIG_ZYC_ROAMING=y
endef

define KernelPackage/zyc_roaming/description
	Kernel driver for zyc roaming
endef

define Build/Configure
endef

define Build/Compile/kmod
	$(MAKE) -C "$(LINUX_DIR)" $(strip $(MAKE_OPTS)) \
		CONFIG_ZYC_ROAMING=m \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		ARCH="$(LINUX_KARCH)" \
		SUBDIRS="$(KPKG_BUILD_DIR)" \
		EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
		modules
endef

define Build/Compile/userspace
	$(MAKE) -C "$(PKG_BUILD_DIR)/userspace_roaming" \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		ARCH="$(LINUX_KARCH)"
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) -rf ./src/* $(PKG_BUILD_DIR)/
	$(Build/Patch)
endef

define Build/Compile
	$(if $(CONFIG_ZYC_ROAMING),$(Build/Compile/kmod))
	$(Build/Compile/userspace)
endef

define Build/Install
endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh
[ -z "$${IPKG_INSROOT}" ] && {
	/etc/init.d/roaming enable
}
exit 0;
endef

define Package/$(PKG_NAME)/config
if PACKAGE_roaming
	config ZYC_ROAMING
		bool 'zyc_roaming'
		default 'y'
endif
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/userspace_roaming/src/roaming $(1)/usr/sbin/
	$(INSTALL_DIR) $(1)/etc
	$(CP) -rf ./files/* $(1)/
endef

$(eval $(call KernelPackage,zyc_roaming))
$(eval $(call BuildPackage,$(PKG_NAME)))
