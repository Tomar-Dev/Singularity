# scripts/resolve_crash.py
import sys
import subprocess
import os

KERNEL_BIN = "../iso/boot/singularity_kernel.bin"
ADDR2LINE = "addr2line"

def main():
    if len(sys.argv) < 2:
        print("Kullanım: python3 resolve_crash.py <RIP_HEX_ADDRESS>")
        print("Örnek: python3 resolve_crash.py 0x10a89f")
        return

    rip = sys.argv[1]
    
    if not os.path.exists(KERNEL_BIN):
        print(f"Hata: Kernel dosyası bulunamadı: {KERNEL_BIN}")
        return

    print(f"Adres Çözümleniyor: {rip} ...")
    
    try:
        result = subprocess.check_output([ADDR2LINE, "-e", KERNEL_BIN, "-f", "-C", "-i", rip],
            stderr=subprocess.STDOUT
        ).decode("utf-8").strip()
        
        print("-" * 40)
        print(result)
        print("-" * 40)
        
    except Exception as e:
        print(f"Hata oluştu: {e}")

if __name__ == "__main__":
    main()