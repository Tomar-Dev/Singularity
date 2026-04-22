# scripts/Makefile
include config.mk

ROOT_DIR   = ..
BUILD_DIR  = $(ROOT_DIR)/build
ISO_DIR    = $(ROOT_DIR)/iso
KERNEL_BIN = $(ISO_DIR)/boot/singularity_kernel.bin
KERNEL_ELF = $(BUILD_DIR)/singularity_kernel.elf
KERNEL_DIS = $(BUILD_DIR)/singularity_kernel.dis
ISO_IMAGE  = $(ROOT_DIR)/singularity.iso

INITRD_TAR = $(ISO_DIR)/boot/rootfs.tar
PCI_DB     = $(BUILD_DIR)/pci.db

SATA_IMG   = $(BUILD_DIR)/sata.img
NVME_IMG   = $(BUILD_DIR)/nvme.img
VIRTIO_IMG = $(BUILD_DIR)/virtio.img
UDF_IMG    = $(BUILD_DIR)/udf.img

ifeq ($(V),1)
    Q =
    CARGO_QUIET =
else
    Q = @
    CARGO_QUIET = --quiet
endif

RUST_DIR    = $(ROOT_DIR)/rust_core
RUST_TARGET = x86_64-singularity
RUST_LIB    = $(RUST_DIR)/target/$(RUST_TARGET)/release/libsingularity_rust.a

QEMU_AUDIO = -audiodev none,id=snd0 -machine pcspk-audiodev=snd0

QEMU_NUMA = -object memory-backend-ram,id=mem0,size=256M \
            -object memory-backend-ram,id=mem1,size=256M \
            -numa node,memdev=mem0,cpus=0-1,nodeid=0 \
            -numa node,memdev=mem1,cpus=2-3,nodeid=1

KVM_EXISTS := $(shell test -r /dev/kvm && echo yes || echo no)

ifeq ($(KVM_EXISTS),yes)
    QEMU_ACCEL = -enable-kvm
    QEMU_CPU   = -cpu host
else
    QEMU_ACCEL =
    QEMU_CPU   = -cpu Skylake-Client-v3,-pcid,-x2apic,-tsc-deadline,-invpcid,-spec-ctrl,-xsavec
endif

INCLUDES = -I$(ROOT_DIR) -I$(ROOT_DIR)/archs -I$(ROOT_DIR)/archs/cpu/x86_64
CFLAGS   += $(INCLUDES)
CXXFLAGS += $(INCLUDES)

C_SOURCES   := $(shell find $(ROOT_DIR) -path $(ROOT_DIR)/build -prune -o \
                        -path $(RUST_DIR) -prune -o -name '*.c'   -print)
CXX_SOURCES := $(shell find $(ROOT_DIR) -path $(ROOT_DIR)/build -prune -o \
                        -path $(RUST_DIR) -prune -o -name '*.cpp' -print)
ASM_SOURCES := $(shell find $(ROOT_DIR) -path $(ROOT_DIR)/build -prune -o \
                        -path $(RUST_DIR) -prune -o -name '*.asm' -print)
RUST_SOURCES := $(shell find $(RUST_DIR)/src -name '*.rs')

# KDB Exclusion
C_SOURCES   := $(filter-out $(ROOT_DIR)/kernel/kdb.c, $(C_SOURCES))

# ---------------------------------------------------------------------------
# C / C++ exclusions
# ---------------------------------------------------------------------------
C_SOURCES   := $(filter-out $(ROOT_DIR)/drivers/vga/%,                 $(C_SOURCES))
C_SOURCES   := $(filter-out $(ROOT_DIR)/kernel/ksyms.c,                $(C_SOURCES))
CXX_SOURCES := $(filter-out $(ROOT_DIR)/system/disk/gpt.cpp,           $(CXX_SOURCES))
CXX_SOURCES := $(filter-out $(ROOT_DIR)/archs/cpu/x86_64/timer/hpet.cpp,        $(CXX_SOURCES))
CXX_SOURCES := $(filter-out $(ROOT_DIR)/archs/cpu/x86_64/timer/pit.cpp,         $(CXX_SOURCES))
CXX_SOURCES := $(filter-out $(ROOT_DIR)/system/shell/utils.cpp,        $(CXX_SOURCES))
CXX_SOURCES := $(filter-out $(ROOT_DIR)/system/fs/%,                   $(CXX_SOURCES))
CXX_SOURCES := $(filter-out $(ROOT_DIR)/archs/fs/%,                    $(CXX_SOURCES))
CXX_SOURCES := $(filter-out $(ROOT_DIR)/system/hal/%,                  $(CXX_SOURCES))
CXX_SOURCES := $(filter-out $(ROOT_DIR)/drivers/pci/pcie.cpp,          $(CXX_SOURCES))

# ---------------------------------------------------------------------------
# ASM exclusions
# ---------------------------------------------------------------------------
ASM_SOURCES := $(filter-out $(ROOT_DIR)/kernel/fastops.asm,                     $(ASM_SOURCES))
ASM_SOURCES := $(filter-out $(ROOT_DIR)/system/security/rng.asm,                $(ASM_SOURCES))
ASM_SOURCES := $(filter-out $(ROOT_DIR)/system/fs/iso9660/iso9660_utils.asm,    $(ASM_SOURCES))
ASM_SOURCES := $(filter-out $(ROOT_DIR)/system/fs/udf/udf_utils.asm,            $(ASM_SOURCES))
ASM_SOURCES := $(filter-out $(ROOT_DIR)/system/shell/torture_asm.asm,           $(ASM_SOURCES))
ASM_SOURCES := $(filter-out $(ROOT_DIR)/system/device/device_asm.asm,           $(ASM_SOURCES))
ASM_SOURCES := $(filter-out $(ROOT_DIR)/system/process/math_helper.asm,         $(ASM_SOURCES))
ASM_SOURCES := $(filter-out $(ROOT_DIR)/archs/cpu/x86_64/smbios/smbios_search.asm,       $(ASM_SOURCES))

C_OBJECTS   := $(patsubst $(ROOT_DIR)/%.c,   $(BUILD_DIR)/%.o, $(C_SOURCES))
CXX_OBJECTS := $(patsubst $(ROOT_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(CXX_SOURCES))
ASM_OBJECTS := $(patsubst $(ROOT_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
ALL_OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS) $(CXX_OBJECTS)

CRITICAL_FILES := \
    kernel/config.o \
    archs/cpu/x86_64/syscall/syscall.o \
    archs/cpu/x86_64/smp/smp.o \
    kernel/debug.o \
    kernel/kernel.o \
    kernel/panic.o \
    libc/stdio.o \
    archs/cpu/x86_64/memory/paging_core.o \
    archs/cpu/x86_64/memory/paging_vmm.o \
    archs/cpu/x86_64/memory/paging_fault.o \
    archs/memory/kheap.o \
    archs/memory/kheap_slab.o \
    archs/kom/common/handle.o \
    archs/kom/common/kom_syscalls.o \
    archs/kom/common/kcontainer.o \
    archs/kom/common/kdevice.o \
    archs/kom/common/kblob.o \
    archs/kom/common/ons.o \
    archs/kom/common/kevent.o \
    system/irq/kir.o \
    archs/kom/common/provider_glue.o \
    system/init/tar_inflater.o \
    system/process/process.o \
    system/process/scheduler.o \
    system/process/stack_asm.o \
    system/process/reaper.o \
    system/security/stack_protector.o \
    drivers/audio/intel_hda.o \
    system/device/virtual_dev.o \
    system/shell/utils/sysinfo.o \
    system/shell/utils/torture.o \
    system/console/console.o \
    system/device/virtio_glue.o \
    drivers/storage/ahci/ahci.o \
    drivers/storage/ahci/ahci_port.o \
    system/sync/mutex.o \
    system/sync/rwlock.o \
    archs/cpu/x86_64/drivers/watchdog/wdt.o \
    system/debug/system_check.o \
    system/init/service.o \
    system/ffi/ffi_stats.o \
    system/disk/mkfs.o \
    archs/cpu/x86_64/hal_cpu_x86_64.o

CRITICAL_OBJECTS := $(addprefix $(BUILD_DIR)/, $(CRITICAL_FILES))
$(CRITICAL_OBJECTS): C_OPT = -O2

BLUE   = \033[1;34m
GREEN  = \033[1;32m
YELLOW = \033[1;33m
RED    = \033[1;31m
RESET  = \033[0m

.PHONY: all run rebuild clean asm pci help tidy

all: $(ISO_IMAGE)

run: $(ISO_IMAGE) $(SATA_IMG) $(NVME_IMG) $(VIRTIO_IMG) $(UDF_IMG)
	@echo "$(GREEN)[RUN]$(RESET) Starting QEMU (UEFI Mode / Q35 Chipset)..."
	@echo "$(BLUE)[INFO]$(RESET) KVM: $(KVM_EXISTS) | CPU: $(QEMU_CPU)"
	$(Q)qemu-system-x86_64 -machine q35 -bios /usr/share/ovmf/OVMF.fd \
		-serial stdio -m 512M -smp 4 \
		$(QEMU_ACCEL) $(QEMU_CPU) \
		-device ahci,id=ahci \
		-device ide-hd,drive=disk,bus=ahci.0 \
		-drive id=disk,file=$(SATA_IMG),format=raw,if=none \
		-device ide-hd,drive=udfdisk,bus=ahci.1 \
		-drive if=none,id=udfdisk,file=$(UDF_IMG),format=raw \
		-device ide-cd,drive=cdrom,bus=ahci.2 \
		-drive id=cdrom,file=$(ISO_IMAGE),format=raw,if=none,media=cdrom,readonly=on \
		-device nvme,serial=deadbeef,drive=nvm \
		-drive file=$(NVME_IMG),format=raw,if=none,id=nvm \
		-drive if=none,id=vdisk,file=$(VIRTIO_IMG),format=raw \
		-device virtio-blk-pci,drive=vdisk \
		-device qemu-xhci,id=xhci \
		-device intel-hda -device hda-duplex,audiodev=snd0 \
		$(QEMU_AUDIO) $(QEMU_NUMA) \
		-watchdog-action reset \
		-global ICH9-LPC.noreboot=false

rebuild: clean all

clean:
	@echo "$(RED)[CLEAN]$(RESET) Removing build files..."
	@COUNT=$$(find $(BUILD_DIR) $(ISO_DIR)/boot $(RUST_DIR)/target $(ISO_IMAGE) \
	         -type f 2>/dev/null | wc -l); \
	SIZE=$$(du -sch $(BUILD_DIR) $(ISO_DIR)/boot $(RUST_DIR)/target $(ISO_IMAGE) \
	       2>/dev/null | tail -n1 | awk '{print $$1}'); \
	if [ -z "$$SIZE" ]; then SIZE="0B"; fi; \
	rm -rf $(BUILD_DIR) $(ISO_IMAGE) $(ISO_DIR)/boot; \
	cd $(RUST_DIR) && cargo clean > /dev/null 2>&1; \
	echo "     Removed $$COUNT files, Freed $$SIZE"; \
	echo "$(GREEN)[OK]$(RESET) Clean complete"

asm: $(KERNEL_ELF)
	@echo "$(BLUE)[DBG]$(RESET) Generating Kernel Disassembly..."
	$(Q)$(OBJDUMP) -d -S -C $(KERNEL_ELF) > $(KERNEL_DIS)
	@echo "$(GREEN)[OK]$(RESET) Disassembly saved to $(KERNEL_DIS)"

pci:
	@echo "$(BLUE)[DB]$(RESET) Updating PCI Database..."
	@mkdir -p $(BUILD_DIR)
	$(Q)python3 update_pci_db.py $(PCI_DB)
	@echo "$(GREEN)[OK]$(RESET) $(PCI_DB) updated. Run 'make all' to embed."

# BUG FIX: Tidy now respects C++ and C files separately to avoid false-positive syntax errors
tidy:
	@echo "$(BLUE)[TIDY]$(RESET) Running Clang-Tidy static analysis..."
	@echo "Checking C files..."
	$(Q)clang-tidy $(C_SOURCES) -- $(CFLAGS) -m64 -ffreestanding
	@echo "Checking C++ files..."
	$(Q)clang-tidy $(CXX_SOURCES) -- $(CXXFLAGS) -m64 -ffreestanding

help:
	@echo ""
	@echo "$(BLUE)Singularity Build System$(RESET)"
	@echo "--------------------------------------------"
	@echo "  make         Build kernel + ISO"
	@echo "  make run     Build + run in QEMU"
	@echo "  make rebuild Full clean + build"
	@echo "  make clean   Remove all build artefacts"
	@echo "  make tidy    Run Clang-Tidy Linter"
	@echo "--------------------------------------------"
	@echo ""

$(RUST_LIB): $(RUST_SOURCES) $(RUST_DIR)/Cargo.toml
	@echo "$(YELLOW)[BUILD]$(RESET) Compiling Native Core Modules..."
	@for f in $(filter %.rs, $?); do echo "$(YELLOW)[RUSTC]$(RESET) $$f"; done
	$(Q)cd $(RUST_DIR) && cargo build $(CARGO_QUIET) --release \
		--target $(RUST_TARGET).json \
		-Z build-std=core,compiler_builtins,alloc \
		-Z json-target-spec

$(KERNEL_BIN): $(ALL_OBJECTS) $(RUST_LIB)
	@echo "$(BLUE)[LLD]$(RESET) Linking (Pass 1)..."
	@mkdir -p $(dir $(KERNEL_BIN))
	$(Q)printf '#include "kernel/ksyms.h"\nconst struct kernel_symbol kernel_symbols[] = {{0, 0}};\nconst uint32_t kernel_symbol_count = 0;\n' \
	     > $(BUILD_DIR)/ksyms_dummy.c
	$(Q)$(CC) $(CFLAGS) $(BUILD_DIR)/ksyms_dummy.c  -o $(BUILD_DIR)/ksyms.o
	$(Q)$(CC) $(CFLAGS) $(ROOT_DIR)/kernel/ksyms.c  -o $(BUILD_DIR)/ksyms_logic.o
	$(Q)$(LD) $(LDFLAGS) -o $(KERNEL_ELF) \
	     $(ALL_OBJECTS) $(BUILD_DIR)/ksyms_logic.o $(BUILD_DIR)/ksyms.o $(RUST_LIB)
	@echo "$(BLUE)[SYM]$(RESET) Generating Symbol Table..."
	$(Q)$(NM) -n $(KERNEL_ELF) > $(BUILD_DIR)/kernel.map
	$(Q)python3 map2sym.py $(BUILD_DIR)/kernel.map > $(BUILD_DIR)/ksyms_data.c
	$(Q)$(CC) $(CFLAGS) $(BUILD_DIR)/ksyms_data.c   -o $(BUILD_DIR)/ksyms.o
	@echo "$(BLUE)[LLD]$(RESET) Finalizing Kernel Binary..."
	$(Q)$(LD) $(LDFLAGS) -o $(KERNEL_ELF) \
	     $(ALL_OBJECTS) $(BUILD_DIR)/ksyms_logic.o $(BUILD_DIR)/ksyms.o $(RUST_LIB)
	$(Q)cp $(KERNEL_ELF) $@
	@$(STRIP) --strip-debug --strip-unneeded $@
	@echo "$(GREEN)[OK]$(RESET) Kernel linked: $@"

$(PCI_DB):
	@mkdir -p $(BUILD_DIR)
	$(Q)python3 update_pci_db.py $(PCI_DB)

$(INITRD_TAR): rootfs_builder.py $(PCI_DB)
	@echo "$(BLUE)[FS]$(RESET) Generating InitRD..."
	$(Q)python3 rootfs_builder.py $(PCI_DB)
	@echo "$(GREEN)[OK]$(RESET) InitRD Ready."

$(ISO_IMAGE): $(KERNEL_BIN) $(INITRD_TAR) $(ROOT_DIR)/boot/grub/grub.cfg
	@echo "$(BLUE)[ISO]$(RESET) Building ISO image..."
	@mkdir -p $(ISO_DIR)/boot/grub
	$(Q)cp $(ROOT_DIR)/boot/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(Q)grub-mkrescue -o $@ $(ISO_DIR) 2>/dev/null
	@echo "$(GREEN)[OK]$(RESET) ISO Ready: $@"

$(BUILD_DIR)/%.o: $(ROOT_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "$(YELLOW)[CLANG++]$(RESET) $<"
	$(Q)$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(ROOT_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(YELLOW)[CLANG]$(RESET) $<"
	$(Q)$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(ROOT_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "$(YELLOW)[NASM]$(RESET) $<"
	$(Q)$(ASM) $(ASMFLAGS) $< -o $@

$(SATA_IMG):
	@echo "$(BLUE)[DISK]$(RESET) Creating SATA disk image..."
	@mkdir -p $(BUILD_DIR)
	$(Q)dd if=/dev/zero of=$(SATA_IMG) bs=1M count=512 status=none
	$(Q)parted -s $(SATA_IMG) mklabel gpt
	$(Q)parted -s $(SATA_IMG) mkpart primary fat32 1MiB 65MiB
	$(Q)parted -s $(SATA_IMG) name 1 "SATA_VOL"
	$(Q)dd if=/dev/zero of=$(BUILD_DIR)/part.img bs=1M count=64 status=none
	$(Q)mkfs.fat -F 32 -n "FAT_VOL" $(BUILD_DIR)/part.img > /dev/null
	$(Q)dd if=$(BUILD_DIR)/part.img of=$(SATA_IMG) bs=1M seek=1 conv=notrunc status=none
	$(Q)rm $(BUILD_DIR)/part.img

$(NVME_IMG):
	@echo "$(BLUE)[DISK]$(RESET) Creating NVMe disk image..."
	@mkdir -p $(BUILD_DIR)
	$(Q)dd if=/dev/zero of=$(NVME_IMG) bs=1M count=256 status=none
	$(Q)parted -s $(NVME_IMG) mklabel gpt
	$(Q)parted -s $(NVME_IMG) mkpart primary ext4 1MiB 33MiB
	$(Q)parted -s $(NVME_IMG) name 1 "NVMe_VOL"
	$(Q)dd if=/dev/zero of=$(BUILD_DIR)/nvme_part.img bs=1M count=32 status=none
	$(Q)mkfs.ext4 -L "EXT4_VOL" -O ^64bit,^metadata_csum -I 256 \
	    $(BUILD_DIR)/nvme_part.img > /dev/null
	$(Q)dd if=$(BUILD_DIR)/nvme_part.img of=$(NVME_IMG) bs=1M seek=1 conv=notrunc status=none
	$(Q)rm $(BUILD_DIR)/nvme_part.img

$(VIRTIO_IMG):
	@echo "$(BLUE)[DISK]$(RESET) Creating VirtIO disk image..."
	@mkdir -p $(BUILD_DIR)
	$(Q)dd if=/dev/zero of=$(VIRTIO_IMG) bs=1M count=128 status=none
	$(Q)parted -s $(VIRTIO_IMG) mklabel gpt
	$(Q)parted -s $(VIRTIO_IMG) mkpart primary ext2 1MiB 17MiB
	$(Q)parted -s $(VIRTIO_IMG) name 1 "VirtIO_VOL"
	$(Q)dd if=/dev/zero of=$(BUILD_DIR)/virtio_part.img bs=1M count=16 status=none
	$(Q)mkfs.ext2 -L "VIRT_VOL" $(BUILD_DIR)/virtio_part.img > /dev/null
	$(Q)dd if=$(BUILD_DIR)/virtio_part.img of=$(VIRTIO_IMG) bs=1M seek=1 conv=notrunc status=none
	$(Q)rm $(BUILD_DIR)/virtio_part.img

$(UDF_IMG):
	@echo "$(BLUE)[DISK]$(RESET) Creating UDF disk image..."
	@mkdir -p $(BUILD_DIR)
	$(Q)dd if=/dev/zero of=$(UDF_IMG) bs=1M count=64 status=none
	$(Q)mkudffs --media-type=hd --blocksize=512 --label="UDF_VOL" $(UDF_IMG) > /dev/null
	@mkdir -p $(BUILD_DIR)/mnt_udf
	$(Q)sudo mount -o loop $(UDF_IMG) $(BUILD_DIR)/mnt_udf || exit 1
	$(Q)sudo sh -c 'echo "Hello from UDF!" > $(BUILD_DIR)/mnt_udf/readme.txt'
	$(Q)sudo sh -c 'mkdir -p $(BUILD_DIR)/mnt_udf/system'
	$(Q)sudo sh -c 'echo "System Config" > $(BUILD_DIR)/mnt_udf/system/config.ini'
	$(Q)sync
	$(Q)sudo umount $(BUILD_DIR)/mnt_udf
	@rmdir $(BUILD_DIR)/mnt_udf
	@echo "$(GREEN)[OK]$(RESET) UDF Image Ready."