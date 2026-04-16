# backup_creator.py
import os
import shutil
import stat
import sys

def remove_readonly(func, path, exc_info):
    """
    Forcefully removes read-only attributes from files (especially .git objects)
    on Windows to resolve access denied errors during deletion.
    """
    try:
        os.chmod(path, stat.S_IWRITE)
        func(path)
    except Exception:
        pass

def create_backup():
    GREEN = "\033[92m"
    RED = "\033[91m"
    RESET = "\033[0m"

    try:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        parent_dir = os.path.dirname(current_dir)
        dir_name = os.path.basename(current_dir)
        
        backup_dir_name = f"{dir_name}_Backup"
        destination_path = os.path.join(parent_dir, backup_dir_name)

        if os.path.exists(destination_path):
            if sys.version_info >= (3, 12):
                shutil.rmtree(destination_path, onexc=remove_readonly)
            else:
                shutil.rmtree(destination_path, onerror=remove_readonly)

        ignore_patterns = shutil.ignore_patterns('.git', '__pycache__', 'build', 'iso', '*.img', '*.iso', '*.bin')
        shutil.copytree(current_dir, destination_path, ignore=ignore_patterns)

        print(f"{GREEN}[BACKUP] Backup created successfully at: {destination_path}{RESET}")

    except Exception as e:
        print(f"{RED}[BACKUP] Backup could not be created due to an error: {e}{RESET}")

if __name__ == "__main__":
    create_backup()