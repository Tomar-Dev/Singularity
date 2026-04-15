#!/bin/bash
# scripts/symbol_check.sh

KERNEL_BIN="../iso/boot/meowkernel.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Kernel binary not found at $KERNEL_BIN"
    exit 0
fi

echo "[CHECK] Verifying Symbol Visibility..."

# Look for undefined symbols (U) that look like mangled C++ names (start with _Z)
# or specific critical C symbols that should be present.

# Since we link statically, there shouldn't be "U" (undefined) symbols for core logic.
# However, the linker might report U for symbols provided by linker script or built-ins.

# Check 1: Ensure critical C hooks are present and not mangled
# We grep for the symbol in the symbol table. It should NOT be "U".
REQUIRED_SYMBOLS="init_keyboard_cpp init_mouse init_ahci init_nvme device_manager_init"

for sym in $REQUIRED_SYMBOLS; do
    if ! nm "$KERNEL_BIN" | grep -q " T $sym"; then
        echo "❌ Critical symbol '$sym' not found or not global (T)."
        # Check if it's mangled
        if nm "$KERNEL_BIN" | grep -q "_Z.*$sym"; then
             echo "   -> Found mangled version! Extern \"C\" missing?"
        fi
        exit 1
    fi
done

echo "✅ Critical C linkage symbols verified."