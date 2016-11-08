export CONFIG_DRM_AMDGPU=m
export BUILD_AS_DKMS=y
export CONFIG_DRM_AMDGPU_CIK=y
export CONFIG_DRM_AMDGPU_USERPTR=y
export CONFIG_DRM_AMD_POWERPLAY=y
export CONFIG_DRM_AMD_DAL=y
export CONFIG_DRM_AMD_DAL_VBIOS_PRESENT=y
export CONFIG_DRM_AMD_DAL_DCE11_0=y
export CONFIG_DRM_AMD_DAL_DCE11_2=y
export CONFIG_DRM_AMD_DAL_DCE10_0=y
export CONFIG_DRM_AMD_DAL_DCE8_0=y

subdir-ccflags-y += -DBUILD_AS_DKMS
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_CIK
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_USERPTR
subdir-ccflags-y += -DCONFIG_DRM_AMD_POWERPLAY
subdir-ccflags-y += -DCONFIG_DRM_AMD_DAL=y
subdir-ccflags-y += -DCONFIG_DRM_AMD_DAL_VBIOS_PRESENT=y
subdir-ccflags-y += -DCONFIG_DRM_AMD_DAL_DCE11_0=y
subdir-ccflags-y += -DCONFIG_DRM_AMD_DAL_DCE11_2=y
subdir-ccflags-y += -DCONFIG_DRM_AMD_DAL_DCE10_0=y
subdir-ccflags-y += -DCONFIG_DRM_AMD_DAL_DCE8_0=y

obj-m += amd/amdgpu/
