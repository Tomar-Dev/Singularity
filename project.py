# project.py
import os
import shutil
import sys

IGNORE_DIRS = {'target', 'build', '.git', '.vscode', '__pycache__', 'rootfs'}

CUSTOM_FILE_MESSAGES = {
    'pci_database.h': 
        'The contents of this file are too long. The contents of the file can be learned from the “...scripts/update_pci_db.py” file.',
    
    'project.py': 
        'This file creates the “project.txt” file. (The contents of files with the following extensions are written: .c, .h, .cpp, .hpp, .txt, .json, .rs, .asm, .py, .ld, .cfg, .toml)',
    
    'backup_creator.py': 
        'This file copies the current state of the project to an external folder named “MeowOS_Backup.” This creates a backup of the project.',
    
    'project.txt': 
        "This file contains all the project's code and directory structure.",

    'clean_comments.py': 
        "Removes unnecessary command lines."
}

def get_directory_structure(path, indent=""):
    structure = "Root_Directory\n"
    try:
        items = sorted(os.listdir(path))
    except OSError:
        return ""

    for item in items:
        if item in IGNORE_DIRS:
            continue

        item_path = os.path.join(path, item)
        structure += f"{indent}|--> {item}\n"

        if os.path.isdir(item_path):
            structure += get_directory_structure_recursive(item_path, indent + "|     ")

    return structure

def get_directory_structure_recursive(path, indent=""):
    structure = ""
    try:
        items = sorted(os.listdir(path))
    except OSError:
        return ""

    for item in items:
        if item in IGNORE_DIRS:
            continue
            
        item_path = os.path.join(path, item)
        structure += f"{indent}|--> {item}\n"
        if os.path.isdir(item_path):
            structure += get_directory_structure_recursive(item_path, indent + "|     ")
    return structure

def get_files_content(path, allowed_extensions):
    content = ""
    for root, dirs, files in os.walk(path):
        dirs[:] = [d for d in dirs if d not in IGNORE_DIRS]
        
        for file in files:
            file_path = os.path.join(root, file)
            relative_path = os.path.relpath(file_path, path)

            if file in CUSTOM_FILE_MESSAGES:
                content += f".../{relative_path}:\n"
                content += CUSTOM_FILE_MESSAGES[file] + "\n\n"
                continue

            file_name, file_ext = os.path.splitext(file)
            
            if file_ext in allowed_extensions or file_name == "Makefile" or file_name == "Cargo.toml":
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        content += f".../{relative_path}:\n"
                        content += f.read() + "\n\n"
                except Exception as e:
                    content += f"Error: {file_path} could not be read - {e}\n\n"
    return content

if __name__ == "__main__":
    GREEN = "\033[92m"
    RED = "\033[91m"
    RESET = "\033[0m"

    try:
        current_directory = os.path.dirname(os.path.abspath(__file__))
        
        allowed_extensions = [
            '.c', '.h', '.hpp', '.txt', '.asm', '.cpp', 
            '.cfg', '.json', '.py', '.ld', 
            '.rs', '.toml', '.mk'
        ]

        dir_structure = get_directory_structure(current_directory)
        files_content = get_files_content(current_directory, allowed_extensions)

        with open("project.txt", "w", encoding="utf-8") as f:
            f.write("Directory Structure:\n")
            f.write(dir_structure)
            f.write("\n")
            f.write(files_content)

        print(f"{GREEN}[PROJECT] Project.txt updated successfully.{RESET}")
        print(f"{GREEN}[INFO] Custom placeholders applied for: {', '.join(CUSTOM_FILE_MESSAGES.keys())}{RESET}")

    except Exception as e:
        print(f"{RED}[PROJECT] Error: {e}{RESET}")