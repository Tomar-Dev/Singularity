# scripts/rootfs_builder.py
import os
import tarfile
import shutil
import sys

def create_rootfs(pci_db_path):
    current_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(current_dir)
    
    build_dir = os.path.join(project_root, "build")
    root_dir = os.path.join(build_dir, "rootfs")
    
    iso_boot_dir = os.path.join(project_root, "iso", "boot")
    output_tar = os.path.join(iso_boot_dir, "rootfs.tar")

    if not os.path.exists(build_dir):
        os.makedirs(build_dir)
        
    if os.path.exists(root_dir):
        shutil.rmtree(root_dir)
        
    os.makedirs(root_dir)
    os.makedirs(os.path.join(root_dir, "system"))
    os.makedirs(os.path.join(root_dir, "home"))
    
    os.makedirs(os.path.join(root_dir, "devices"))
    os.makedirs(os.path.join(root_dir, "volumes"))
    os.makedirs(os.path.join(root_dir, "ramdisk"))
    
    if not os.path.exists(iso_boot_dir):
        os.makedirs(iso_boot_dir)

    print(f"[BUILD] Creating RootFS files in {root_dir}...")

    if pci_db_path and os.path.exists(pci_db_path):
        shutil.copy2(pci_db_path, os.path.join(root_dir, "system", "pci.db"))
        print("[BUILD] PCI Database embedded into RootFS.")
    else:
        print("[WARNING] PCI Database (pci.db) not found. Skipping embedding.")

    with open(os.path.join(root_dir, "hello.txt"), "w") as f:
        f.write("Hello from MeowOS InitRD! The ONS (Object Namespace) is working properly.\n")

    with open(os.path.join(root_dir, "system", "version.txt"), "w") as f:
        f.write("MeowOS Kernel v6.9 (Native KOM Edition)\n")

    with open(os.path.join(root_dir, "system", "readme.txt"), "w") as f:
        f.write("This is a system directory.\nDo not delete critical files.\n")

    with open(os.path.join(root_dir, "system", "startup.cfg"), "w") as f:
        f.write("silent_run \"Creating Volatile Storage\" ramdisk\n") 
        f.write("silent_run \"Auto-Mounting Volumes\" automount\n") 

    with open(os.path.join(root_dir, "home", "config.cfg"), "w") as f:
        f.write("user=meow\ntheme=dark\nkeyboard=us\n")

    with tarfile.open(output_tar, "w") as tar:
        tar.add(root_dir, arcname=".") 

    print(f"[BUILD] RootFS TAR created at {output_tar}")

if __name__ == "__main__":
    pci_db = sys.argv[1] if len(sys.argv) > 1 else None
    create_rootfs(pci_db)
