# scripts/update_pci_db.py
import urllib.request
import os
import sys
import struct

PCI_IDS_URL = "https://pci-ids.ucw.cz/v2.2/pci.ids"

MANUAL_VENDORS = {
    0x1234: "QEMU/Bochs (Generic)",
    0x80ee: "Oracle Corporation (VirtualBox)",
    0x1b36: "Red Hat, Inc. (QEMU)",
    0x1af4: "Red Hat, Inc. (VirtIO)"
}

MANUAL_DEVICES = {
    0x1234: { 0x1111: "QEMU Standard VGA", 0x100e: "QEMU e1000 NIC" },
    0x80ee: { 0xcafe: "VirtualBox Guest Service", 0xbeef: "VirtualBox Graphics Adapter" },
    0x1b36: { 0x0010: "QEMU NVMe Controller", 0x000d: "QEMU xHCI Controller" }
}

def generate_database(output_file):
    print(f"[DB] Downloading full PCI ID database from {PCI_IDS_URL}...")
    try:
        with urllib.request.urlopen(PCI_IDS_URL) as response:
            data = response.read().decode('utf-8', errors='ignore').splitlines()
    except Exception as e:
        print(f"[ERROR] Download failed: {e}")
        sys.exit(1)

    print("[DB] Parsing Vendors and Devices...")

    parsed_vendors = {}
    parsed_devices = {}
    
    current_vendor_id = None

    for line in data:
        if not line or line.startswith("#"): continue
        
        if not line.startswith("\t"):
            parts = line.split("  ", 1)
            if len(parts) == 2:
                vid_str, name = parts[0].strip(), parts[1].strip()
                try:
                    vid = int(vid_str, 16)
                    current_vendor_id = vid
                    parsed_vendors[vid] = name
                    parsed_devices[vid] = {}
                except:
                    current_vendor_id = None

        elif line.startswith("\t") and not line.startswith("\t\t") and current_vendor_id is not None:
            parts = line.strip().split("  ", 1)
            if len(parts) == 2:
                did_str, dname = parts[0].strip(), parts[1].strip()
                try:
                    did = int(did_str, 16)
                    parsed_devices[current_vendor_id][did] = dname
                except: pass

    for vid, vname in MANUAL_VENDORS.items():
        parsed_vendors[vid] = vname
        if vid not in parsed_devices: parsed_devices[vid] = {}
            
    for vid, devs in MANUAL_DEVICES.items():
        if vid in parsed_devices:
            for did, dname in devs.items():
                parsed_devices[vid][did] = dname

    sorted_vids = sorted(parsed_vendors.keys())
    
    flat_devices =[]
    for vid in sorted_vids:
        for did in sorted(parsed_devices[vid].keys()):
            flat_devices.append((vid, did, parsed_devices[vid][did]))

    string_pool = bytearray()
    
    def add_string(s):
        offset = len(string_pool)
        string_pool.extend(s.encode('utf-8', errors='replace') + b'\x00')
        return offset

    vendor_entries = bytearray()
    for vid in sorted_vids:
        name = parsed_vendors[vid]
        offset = add_string(name)
        vendor_entries.extend(struct.pack('<IH', offset, vid))
        
    vendor_entries = bytearray()
    for vid in sorted_vids:
        offset = add_string(parsed_vendors[vid])
        vendor_entries.extend(struct.pack('<HI', vid, offset))

    device_entries = bytearray()
    for vid, did, name in flat_devices:
        offset = add_string(name)
        device_entries.extend(struct.pack('<HHI', vid, did, offset))

    num_vendors = len(sorted_vids)
    num_devices = len(flat_devices)
    header_size = 16
    string_table_offset = header_size + len(vendor_entries) + len(device_entries)

    header = struct.pack('<4sIII', b'PCID', num_vendors, num_devices, string_table_offset)

    with open(output_file, "wb") as f:
        f.write(header)
        f.write(vendor_entries)
        f.write(device_entries)
        f.write(string_pool)

    print(f"[SUCCESS] Database generated at {output_file} (Size: {len(header) + string_table_offset + len(string_pool)} bytes)")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: update_pci_db.py <output_file>")
        sys.exit(1)
    generate_database(sys.argv[1])