# scripts/config.mk
# Singularity LLVM/Clang Toolchain Configuration

CC = clang
CXX = clang++
ASM = nasm
# Linker olarak Clang'i çağırıyoruz, ancak alt tarafta LLD kullanmasını emredeceğiz.
LD = clang++
OBJDUMP = llvm-objdump
NM = llvm-nm
STRIP = llvm-strip

# Optimizasyon Seviyesi
C_OPT ?= -O3

# Link Time Optimization (Sadece C/C++ Tarafı İçin ThinLTO)
LTO_FLAGS = -flto=thin

# Uyarılar (Clang'e özel deprecation uyarılarını kapatıyoruz)
WARN_FLAGS = -Wall -Wextra -Werror=format-security -Wno-unused-parameter -Wno-deprecated-volatile

# Güvenlik Zırhları
SEC_FLAGS = -fstack-protector-all -mno-red-zone -fcf-protection=full -fno-strict-aliasing

# Mimari
TARGET_FLAGS = -m64 -mcmodel=kernel -ffreestanding -fno-pie

# Ortak Bayraklar
COMMON_FLAGS = $(TARGET_FLAGS) $(C_OPT) $(LTO_FLAGS) $(WARN_FLAGS) $(SEC_FLAGS) -I$(ROOT_DIR)

# Dil Standartları
CFLAGS = $(COMMON_FLAGS) -std=gnu2x -c
CXXFLAGS = $(COMMON_FLAGS) -std=c++2b -c -fno-exceptions -fno-rtti \
           -fno-use-cxa-atexit -fno-threadsafe-statics

# NASM format bayrağı
ASMFLAGS = -f elf64

# FIX: -fuse-ld=lld bayrağı eklendi. Clang'in eski GNU ld veya gold plugin çağırmasını engeller!
LDFLAGS = $(TARGET_FLAGS) $(C_OPT) $(LTO_FLAGS) -fuse-ld=lld -nostdlib -no-pie \
          -Wl,-z,noexecstack -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 \
          -Wl,-T,$(ROOT_DIR)/kernel/linker.ld