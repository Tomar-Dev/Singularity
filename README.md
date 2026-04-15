\# Singularity OS Kernel



Singularity is a modern, clean-slate, 64-bit hybrid operating system built from scratch. It is \*\*not a Linux clone\*\*. Designed with inspiration from modern microkernels (like Zircon/Fuchsia and seL4), Singularity completely discards legacy baggage like Real Mode, 32-bit Protected Mode, Legacy BIOS, and the traditional POSIX Virtual File System (VFS).



Instead, it relies on a \*\*Kernel Object Model (KOM)\*\*, a \*\*Scalable Core Scheduler (SCS)\*\*, and a \*\*Zero-Overhead FFI\*\* bridging C++23 and Rust 2024 via LLVM Cross-Language LTO.



\## 🚀 Key Architecture \& Features



\### 1. Hybrid Core (C++23 \& Rust 2024)

\*   \*\*Zero-Overhead FFI:\*\* Compiled using Clang/LLD with Cross-Language LTO (`-C linker-plugin-lto`). C++ and Rust code are merged at the LLVM IR level, eliminating FFI call overhead.

\*   \*\*Safe Subsystems:\*\* Critical subsystems like the Physical Memory Manager (PMM), Scheduler, and Storage drivers (NVMe, Ext4, FAT32) are written in Rust for memory and thread safety.

\*   \*\*Hardware Abstraction Layer (HAL):\*\* Drivers are entirely decoupled from x86\_64 specific assembly, paving the way for future ARM64/RISC-V ports.



\### 2. Kernel Object Model (KOM) \& ONS

\*   \*\*Everything is a KObject:\*\* Replaces the traditional VFS. Devices, blobs (files), containers (directories), and events are all reference-counted objects (`KObject`).

\*   \*\*Object Namespace (ONS):\*\* A native, lock-free hierarchical tree providing O(1) in-RAM caching and resolution.

\*   \*\*Zero-Copy DMA \& Direct I/O:\*\* Storage drivers use Extent Coalescing to map contiguous disk blocks directly to RAM via `StorageKioVec`, bypassing CPU memory copying completely.



\### 3. Scalable Core Scheduler (SCS)

\*   \*\*100% Lock-Free Dispatcher:\*\* No global process locks. Each CPU core manages its own runqueue and sleep queue.

\*   \*\*Work Stealing:\*\* Idle cores aggressively steal tasks from overloaded neighbors.

\*   \*\*OOM Reaper:\*\* Graceful out-of-memory handling that sacrifices non-critical tasks to prevent system deadlocks.



\### 4. Advanced Memory Management

\*   \*\*Extent-Based PMM:\*\* Buddy-like allocator written in Rust with per-CPU magazines, eliminating internal fragmentation and lock contention.

\*   \*\*Security \& Hardening:\*\* Features include W^X memory protection, ASLR for the Kernel Heap, Stack Canaries (seeded by hardware RNG), and GWP-ASAN (Guard Page Allocator) to catch Use-After-Free and Buffer Overflows.



\### 5. Hardware \& Driver Support

\*   \*\*Boot:\*\* Native 64-bit UEFI entry (Multiboot2).

\*   \*\*Storage:\*\* NVMe (PCIe SSD), AHCI (SATA), VirtIO Block.

\*   \*\*File Systems:\*\* Ext4 (Read), FAT32 (Read/Write/Format), ISO9660, UDF.

\*   \*\*Graphics:\*\* UEFI GOP with Double Buffering, Lock-free rendering, and a secondary GUI mode.

\*   \*\*Audio \& USB:\*\* Intel HDA (High Definition Audio) and xHCI (USB 3.0) root hub enumeration.

\*   \*\*Multicore:\*\* Full SMP support with ACPI/SRAT NUMA topology awareness and x2APIC/IO-APIC routing.



\## 🛠️ Prerequisites



To build Singularity, you need a modern LLVM toolchain and Rust Nightly:

\*   \*\*LLVM/Clang 18+\*\* (`clang`, `clang++`, `lld`, `llvm-objdump`)

\*   \*\*Rust Nightly\*\* (Edition 2024) with `rust-src` component

\*   \*\*NASM\*\* (Assembler)

\*   \*\*Python 3\*\* (For PCI database generation and build scripts)

\*   \*\*Xorriso \& GRUB\*\* (`grub-mkrescue` for ISO generation)

\*   \*\*QEMU \& OVMF\*\* (For testing and emulation)



\## ⚙️ Building and Running



Clone the repository and use the provided Makefile:



```bash

\# Clone the repository

git clone https://github.com/Tomar-Dev/Singularity.git

cd Singularity



\# Build the kernel and generate the bootable ISO

make all



\# Build and run directly in QEMU (Q35 chipset + UEFI)

make run

```



\### Additional Make Targets

\*   `make clean`: Removes all build artifacts.

\*   `make asm`: Generates a complete disassembly of the kernel (`build/singularity\_kernel.dis`) for debugging.

\*   `make tidy`: Runs Clang-Tidy static analysis on the C/C++ codebase.



\## 🖥️ Shell Commands



Once booted, the Singularity Shell provides various commands for system management:

\*   `system`: Displays full system hardware, topology, and memory reports.

\*   `taskmgr`: Real-time CPU core usage and memory statistics.

\*   `disks` / `parts`: Lists attached physical storage devices and their partitions.

\*   `fdisk` / `mkfs`: Manage GPT partitions and format volumes (Requires booting with `lockdown=0`).

\*   `torture`: Runs the built-in stress test (Math, Heap Chaos, Scheduler Bomb, Disk Thrash).



\## 📜 License



This project is licensed under the \*\*GNU GPLv3\*\* License. See the `LICENSE` file for details.

