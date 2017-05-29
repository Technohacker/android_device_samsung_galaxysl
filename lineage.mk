# Inherit Omni GSM telephony parts
$(call inherit-product, vendor/cm/config/telephony.mk)

# Inherit from the common Open Source product configuration
$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base_telephony.mk)

# Inherit from our Lineage product configuration
$(call inherit-product, vendor/cm/config/common.mk)

# Release name
PRODUCT_RELEASE_NAME := SGSL

# Inherit device configuration
$(call inherit-product, device/samsung/galaxysl/full_galaxysl.mk)

## Device identifier. This must come after all inclusions
PRODUCT_DEVICE := galaxysl
PRODUCT_NAME := lineage_galaxysl
PRODUCT_BRAND := Samsung
PRODUCT_MODEL := GT-I9003
