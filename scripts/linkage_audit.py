# scripts/linkage_audit.py
import glob
import sys
import os

def check_linkage():
    print("[AUDIT] Starting Linkage Audit...")
    errors = []
    
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    
    search_paths = [
        os.path.join(root_dir, "drivers/**/*.h"),
        os.path.join(root_dir, "drivers/**/*.hpp"),
        os.path.join(root_dir, "system/**/*.h"),
        os.path.join(root_dir, "system/**/*.hpp")
    ]
    
    files = []
    for path in search_paths:
        files.extend(glob.glob(path, recursive=True))

    for header in files:
        with open(header, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            
            if 'class ' in content:
                open_blocks = content.count('extern "C" {')
                close_blocks = content.count('}')
                
                if open_blocks == 0:
                    errors.append(f"[WARNING] {os.path.basename(header)}: C++ class found but no extern \"C\" block.")
                elif open_blocks != content.count('extern "C"'):
                     pass

                if 'extern "C" {' in content and not '}' in content.split('extern "C" {')[-1]:
                     errors.append(f"[ERROR] {os.path.basename(header)}: Malformed or unclosed extern \"C\" block.")

    if errors:
        print("\n".join(errors))
    else:
        print("[AUDIT] Linkage check passed.")

if __name__ == "__main__":
    check_linkage()