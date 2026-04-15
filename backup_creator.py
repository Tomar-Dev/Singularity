# backup_creator.py
import os
import shutil
import sys

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
            shutil.rmtree(destination_path)

        shutil.copytree(current_dir, destination_path)

        print(f"{GREEN}[BACKUP] Backup created successfully at: {destination_path}{RESET}")

    except Exception as e:
        print(f"{RED}[BACKUP] Backup could not be created due to an error: {e}{RESET}")

if __name__ == "__main__":
    create_backup()