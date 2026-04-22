import os
import shutil

# İç içe geçmiş hatalı klasör yolları ve olmaları gereken doğru yollar
FIX_DIRS = {
    "archs/cpu/x86_64/acpi/acpi": "archs/cpu/x86_64/acpi",
    "archs/cpu/x86_64/apic/apic": "archs/cpu/x86_64/apic",
    "archs/cpu/x86_64/timer/timer": "archs/cpu/x86_64/timer",
    "archs/cpu/x86_64/drivers/rtc/rtc": "archs/cpu/x86_64/drivers/rtc",
    "archs/cpu/x86_64/drivers/keyboard/keyboard": "archs/cpu/x86_64/drivers/keyboard",
    "archs/cpu/x86_64/drivers/mouse/mouse": "archs/cpu/x86_64/drivers/mouse",
    "archs/cpu/x86_64/drivers/misc/misc": "archs/cpu/x86_64/drivers/misc",
    "archs/cpu/x86_64/drivers/serial/serial": "archs/cpu/x86_64/drivers/serial",
    "archs/cpu/x86_64/drivers/watchdog/watchdog": "archs/cpu/x86_64/drivers/watchdog",
    "archs/cpu/x86_64/drivers/bus/bus": "archs/cpu/x86_64/drivers/bus",
    "archs/cpu/x86_64/smbios/smbios": "archs/cpu/x86_64/smbios"
}

def main():
    print("[FIX] İç içe geçmiş klasörler onarılıyor...")

    for bad_dir, good_dir in FIX_DIRS.items():
        if os.path.exists(bad_dir):
            # İçerideki tüm dosyaları bir üst klasöre (olması gereken yere) taşı
            for item in os.listdir(bad_dir):
                src_path = os.path.join(bad_dir, item)
                dst_path = os.path.join(good_dir, item)
                shutil.move(src_path, dst_path)
            
            # İçi boşalan hatalı klasörü sil
            os.rmdir(bad_dir)
            print(f"Düzeltildi: {bad_dir} -> {good_dir}")

    print("\n[OK] Dizin yapısı başarıyla onarıldı!")
    print("Lütfen derlemeyi temizleyip tekrar başlatın: 'make rebuild' veya 'make run'")

if __name__ == "__main__":
    main()