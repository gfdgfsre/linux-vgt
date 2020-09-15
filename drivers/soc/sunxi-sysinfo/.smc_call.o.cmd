cmd_drivers/soc/sunxi-sysinfo/smc_call.o := /aosp/buildroot/buildroot-2020.02.4/output/host/bin/aarch64-buildroot-linux-musl-gcc -Wp,-MD,drivers/soc/sunxi-sysinfo/.smc_call.o.d  -nostdinc -isystem /aosp/buildroot/buildroot-2020.02.4/output/host/lib/gcc/aarch64-buildroot-linux-musl/8.4.0/include -I./arch/arm64/include -I./arch/arm64/include/generated  -I./include -I./arch/arm64/include/uapi -I./arch/arm64/include/generated/uapi -I./include/uapi -I./include/generated/uapi -include ./include/linux/kconfig.h -D__KERNEL__ -mlittle-endian -D__ASSEMBLY__ -fno-PIE -DCONFIG_AS_LSE=1 -mabi=lp64 -DCC_HAVE_ASM_GOTO -Wa,-gdwarf-2   -c -o drivers/soc/sunxi-sysinfo/smc_call.o drivers/soc/sunxi-sysinfo/smc_call.S

source_drivers/soc/sunxi-sysinfo/smc_call.o := drivers/soc/sunxi-sysinfo/smc_call.S

deps_drivers/soc/sunxi-sysinfo/smc_call.o := \
    $(wildcard include/config/arm.h) \
    $(wildcard include/config/arm64.h) \
  include/linux/linkage.h \
  include/linux/compiler.h \
    $(wildcard include/config/trace/branch/profiling.h) \
    $(wildcard include/config/profile/all/branches.h) \
    $(wildcard include/config/stack/validation.h) \
    $(wildcard include/config/kasan.h) \
    $(wildcard include/config/enable/must/check.h) \
    $(wildcard include/config/enable/warn/deprecated.h) \
  include/linux/stringify.h \
  include/linux/export.h \
    $(wildcard include/config/have/underscore/symbol/prefix.h) \
    $(wildcard include/config/modules.h) \
    $(wildcard include/config/modversions.h) \
    $(wildcard include/config/module/rel/crcs.h) \
    $(wildcard include/config/trim/unused/ksyms.h) \
    $(wildcard include/config/unused/symbols.h) \
  arch/arm64/include/asm/linkage.h \

drivers/soc/sunxi-sysinfo/smc_call.o: $(deps_drivers/soc/sunxi-sysinfo/smc_call.o)

$(deps_drivers/soc/sunxi-sysinfo/smc_call.o):
