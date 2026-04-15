# scripts/resolve_symbol.py
import sys
import subprocess
import os

KERNEL_BIN = "../iso/boot/meowkernel.bin"
ADDR2LINE = "addr2line"

def resolve_addr(addr):
    try:
        result = subprocess.check_output(
            [ADDR2LINE, "-e", KERNEL_BIN, "-f", "-C", "-i", addr],
            stderr=subprocess.STDOUT
        ).decode("utf-8").strip()
        return result.splitlines()
    except Exception as e:
        return [f"Error resolving {addr}", str(e)]

def main():
    if len(sys.argv) < 2:
        print("Kullanım: python3 resolve_symbol.py <hex_address> [hex_address ...]")
        return

    if not os.path.exists(KERNEL_BIN):
        print(f"Hata: Kernel dosyası bulunamadı: {KERNEL_BIN}")
        return

    print(f"Adresler çözümleniyor: {KERNEL_BIN}...\n")

    for addr in sys.argv[1:]:
        print(f"Adres: {addr}")
        lines = resolve_addr(addr)
        for l in lines:
            print(f"  {l}")
        print("-" * 30)

if __name__ == "__main__":
    main()