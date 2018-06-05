#
# Copyright (C) 2014 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=beacon
PKG_VERSION:=1.0
PKG_RELEASE=$(PKG_SOURCE_VERSION)

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=ZYC beacon 
  DEPENDS:=+libpcap +libcrypto +libopenssl +libnl-tiny
  MAINTAINER:= ZYC beacon and capture program
endef

#TARGET_LDFLAGS += -L$(STAGING_DIR)/usr/lib/libev
#TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include
TARGET_CPPFLAGS += -I$(STAGING_DIR)/usr/include/libnl-tiny \
				   -DCONFIG_LIBNL20

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(PKG_UNPACK)
	$(Build/Patch)
endef

define Package/$(PKG_NAME)/description
	send beacon frame to transmit message
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/sbin 
	$(INSTALL_DIR) $(1)/usr/bin 
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/beacon $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/capture $(1)/usr/sbin/
	$(INSTALL_BIN) ./files/wds-son $(1)/usr/bin/
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
