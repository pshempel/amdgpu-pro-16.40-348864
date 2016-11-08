ifneq (,$(BUILD_AS_DKMS))

ifneq (,$(wildcard /etc/os-release))
OS_NAME = "$(shell sed -n 's/^ID=\(.*\)/\1/p' /etc/os-release)"
OS_VERSION = $(shell sed -n 's/^VERSION_ID=\(.*\)/\1/p' /etc/os-release)
else ifneq (,$(wildcard /etc/redhat-release))
OS_NAME = "rhel"
OS_VERSION = "$(shell cut -d ' ' -f 7 /etc/redhat-release)"
else
OS_NAME = "unknown"
OS_VERSION = "0.0"
endif

ifeq ("rhel",$(OS_NAME))
ccflags-y += -DOS_NAME_RHEL
else ifeq ("ubuntu",$(OS_NAME))
ccflags-y += -DOS_NAME_UBUNTU
else ifeq ("steamos",$(OS_NAME))
ccflags-y += -DOS_NAME_STEAMOS
else
ccflags-y += -DOS_NAME_UNKNOWN
endif

ccflags-y += \
	-DOS_VERSION_MAJOR=$(shell echo $(OS_VERSION).0 | cut -d. -f1) \
	-DOS_VERSION_MINOR=$(shell echo $(OS_VERSION).0 | cut -d. -f2)

ifeq ($(OS_NAME),"ubuntu")
OS_BUILD_NUM = $(shell echo $(KERNELRELEASE) | cut -d '-' -f 2)
ccflags-y += -DUBUNTU_BUILD_NUM=$(OS_BUILD_NUM)
ifeq ($(OS_VERSION),"14.04")
ccflags-y += -DOS_NAME_UBUNTU_1404
else
ccflags-y += -DOS_NAME_UBUNTU_1604
endif
endif

ifeq ($(OS_NAME),"rhel")
ifeq ($(OS_VERSION),"6.8")
ccflags-y += -DOS_NAME_RHEL_6
ccflags-y += \
	-I$(FULL_AMD_PATH)/backport/rhel/6.8/include \
	-I$(FULL_AMD_PATH)/backport/rhel/6.8/include/uapi
endif
endif

ifeq ($(OS_NAME),"rhel")
ifeq ($(OS_VERSION),"7.2")
ccflags-y += -DOS_NAME_RHEL_7
endif
endif

endif

BACKPORT_OBJS = kcl_drm.o kcl_ttm.o kcl_amdgpu.o kcl_fence.o kcl_mn.o

amdgpu-y += $(addprefix ../backport/,$(BACKPORT_OBJS))

ccflags-y += \
	-I$(FULL_AMD_PATH)/backport/include \
	-include $(FULL_AMD_PATH)/backport/backport.h
