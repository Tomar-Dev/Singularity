# scripts/sys_asm_debug.py
import sys
import re
import os
import argparse
import json
from datetime import datetime

try:
    from rich.console import Console
    from rich.table import Table
    from rich.panel import Panel
    from rich.progress import Progress, SpinnerColumn, BarColumn, TextColumn
    from rich import print as rprint
except ImportError:
    print("HATA: Bu script 'rich' kütüphanesine ihtiyaç duyar.")
    print("Lütfen yükleyin: pip install rich")
    sys.exit(1)

try:
    import cxxfilt
    HAS_CXXFILT = True
except ImportError:
    HAS_CXXFILT = False

KERNEL_DIS = "../build/meowkernel.dis"
console = Console()

VOLATILE_REGS =["rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"]
NON_VOLATILE_REGS =["rbx", "rbp", "rsp", "r12", "r13", "r14", "r15"]
ALL_REGS = VOLATILE_REGS + NON_VOLATILE_REGS

ALLOC_FUNCS =[
    "kmalloc", "kmalloc_aligned", "kmalloc_contiguous", 
    "operator new", "malloc", "calloc", "realloc", 
    "pmm_alloc_frame", "vmm_alloc_stack", "kcalloc", "krealloc",
    "vmm_alloc_pages"
]

FREE_FUNCS =[
    "kfree", "kfree_aligned", "kfree_contiguous", 
    "operator delete", "free", "pmm_free_frame",
    "vmm_free_pages"
]

LOCK_ACQUIRE =["spinlock_acquire", "mutex_lock", "IrqSpinlock::lock"]
LOCK_RELEASE =["spinlock_release", "mutex_unlock", "IrqSpinlock::unlock"]

ISR_UNSAFE_FUNCS =[
    "kmalloc", "free", "sprintf", "timer_sleep", 
    "mutex_lock", "copy_from_user"
]

IRQ_SAFE_WHITELIST =[
    "spinlock_acquire", "irq_handler", "isr_handler", 
    "switch_to_task", "trampoline", "arch_disable_interrupts",
    "enter_user_mode", "syscall_entry", "IrqSpinlock",
    "Shell::", "cmd_", "processCommand", "rust_pmm",
    "rust_serial_write"
]

LOOP_WHITELIST =[
    "panic_at", "kmain", "system_shutdown", "idle_task", 
    "ap_startup", "process_exit", "__stack_chk_fail", 
    "page_fault_handler", "die", "hang", "kthread_starter"
]

IGNORE_PREFIXES =[
    "core::", "alloc::", "compiler_builtins::", "__rust", 
    "rust_begin_unwind", "fmt::", "__ubsan", "__stack_chk", 
    "std::", "meow_rust::", "_start", "start", "isr_common_stub",
    "drop_in_place"
]

OWNERSHIP_TRANSFER_WHITELIST =[
    "NVMeDriver::init", "AHCIDriver::init", "tss_install", "init_uefi",
    "Shell::processCommand", "DeviceManager::registerDevice", "PartitionDevice",
    "Shell::cmd_fdisk", "KernelHeap", "vmm_alloc", "allocator"
]

ISR_VIOLATION_WHITELIST =[
    "page_fault_handler", "panic_at", "gpf_handler", "double_fault_handler",
    "Shell::", "cmd_", "processCommand", "timer_handler", "fpu_exception_handler",
    "isr_handler"
]

class MachineState:
    def __init__(self):
        self.regs = {r: "EMPTY" for r in ALL_REGS}
        self.reg_origins = {r: None for r in ALL_REGS}
        self.reg_alias = {r: 0 for r in ALL_REGS}      
        self.stack = {} 
        self.stack_origins = {}
        self.stack_alias = {}
        self.next_alias_id = 1

    def _propagate_freed(self, alias_id):
        if alias_id == 0: return
        for r in ALL_REGS:
            if self.reg_alias[r] == alias_id:
                self.regs[r] = "FREED"
        for offset in self.stack:
            if self.stack_alias.get(offset) == alias_id:
                self.stack[offset] = "FREED"

    def set_reg_allocated(self, reg, line_num):
        if reg in self.regs:
            self.regs[reg] = "ALLOCATED"
            self.reg_origins[reg] = line_num
            self.reg_alias[reg] = self.next_alias_id
            self.next_alias_id += 1

    def free_reg(self, reg):
        if reg in self.regs:
            alias = self.reg_alias[reg]
            self.regs[reg] = "FREED" 
            if alias > 0:
                self._propagate_freed(alias)
            self.clear_reg(reg) 

    def move_reg_to_reg(self, src, dst):
        if src in self.regs and dst in self.regs:
            self.regs[dst] = self.regs[src]
            self.reg_origins[dst] = self.reg_origins[src]
            self.reg_alias[dst] = self.reg_alias[src]

    def move_reg_to_stack(self, src_reg, offset):
        if src_reg in self.regs:
            self.stack[offset] = self.regs[src_reg]
            self.stack_origins[offset] = self.reg_origins[src_reg]
            self.stack_alias[offset] = self.reg_alias[src_reg]

    def move_stack_to_reg(self, offset, dst_reg):
        if offset in self.stack and dst_reg in self.regs:
            self.regs[dst_reg] = self.stack[offset]
            self.reg_origins[dst_reg] = self.stack_origins[offset]
            self.reg_alias[dst_reg] = self.stack_alias[offset]
        else:
            self.clear_reg(dst_reg)

    def clear_reg(self, reg):
        if reg in self.regs:
            self.regs[reg] = "EMPTY"
            self.reg_origins[reg] = None
            self.reg_alias[reg] = 0

    def invalidate_volatile(self):
        for r in VOLATILE_REGS:
            self.clear_reg(r)

    def get_reg_state(self, reg):
        return self.regs.get(reg, "UNKNOWN")

    def get_reg_origin(self, reg):
        return self.reg_origins.get(reg, "?")

class FunctionAnalyzer:
    def __init__(self, name, addr):
        self.name = name
        self.addr = addr
        self.instructions = []
        self.issues =[]
        self.demangled_name = name
        if HAS_CXXFILT:
            try: self.demangled_name = cxxfilt.demangle(name)
            except: pass

    def add_instruction(self, addr, asm_line):
        self.instructions.append({'addr': addr, 'asm': asm_line})

    def is_ignored(self):
        for prefix in IGNORE_PREFIXES:
            if self.demangled_name.startswith(prefix): return True
        return False

    def analyze(self):
        if self.is_ignored(): return

        state = MachineState()
        lock_depth = 0
        interrupts_disabled = False
        
        is_isr = any(x in self.demangled_name.lower() for x in ["isr", "irq", "handler"])
        
        if is_isr:
            for w in ISR_VIOLATION_WHITELIST:
                if w in self.demangled_name: 
                    is_isr = False
                    break

        if len(self.instructions) > 0 and "cli" in self.instructions[0]['asm']:
            interrupts_disabled = True

        re_stack = re.compile(r'(-?0x[0-9a-f]+|-?\d+)\(%([a-z0-9]+)\)')

        for i, instr in enumerate(self.instructions):
            line = instr['asm'].strip()
            addr = instr['addr']
            parts = line.split(maxsplit=1)
            if not parts: continue
            
            opcode = parts[0]
            operands = parts[1] if len(parts) > 1 else ""
            
            if opcode == "call" or opcode == "callq":
                target = ""
                match = re.search(r'<(.*)>', operands)
                if match: target = match.group(1)
                
                if is_isr:
                    for unsafe in ISR_UNSAFE_FUNCS:
                        if unsafe in target:
                            self.issues.append(f"ISR VIOLATION: Calling unsafe '{unsafe}' inside interrupt handler.")

                if any(x in target for x in ALLOC_FUNCS):
                    if state.get_reg_state("rax") == "ALLOCATED":
                        origin = state.get_reg_origin("rax")
                        is_whitelisted = False
                        for w in OWNERSHIP_TRANSFER_WHITELIST:
                            if w in self.demangled_name: is_whitelisted = True; break
                        
                        if not is_whitelisted:
                            self.issues.append(f"MEMORY LEAK: 0x{addr:x} -> RAX overwritten by new alloc. (Prev alloc at line {origin})")
                    
                    state.invalidate_volatile() 
                    state.set_reg_allocated("rax", i)

                elif any(x in target for x in FREE_FUNCS):
                    status = state.get_reg_state("rdi")
                    if status == "FREED":
                        self.issues.append(f"DOUBLE FREE: 0x{addr:x} -> {target} on freed pointer (RDI).")
                    elif status == "ALLOCATED":
                        state.free_reg("rdi")
                    state.invalidate_volatile()

                elif any(x in target for x in LOCK_ACQUIRE):
                    lock_depth += 1
                    state.invalidate_volatile()
                elif any(x in target for x in LOCK_RELEASE):
                    if lock_depth <= 0:
                        lock_depth = 0 
                    else:
                        lock_depth -= 1
                    state.invalidate_volatile()
                
                elif "panic" in target or "exit" in target or "die" in target:
                    return 
                
                else:
                    state.invalidate_volatile()

            elif opcode in["mov", "movq", "movl", "movabs"]:
                ops = operands.split(',')
                if len(ops) == 2:
                    src_op = ops[0].strip()
                    dst_op = ops[1].strip()
                    
                    src_reg = src_op.replace('%', '')
                    dst_reg = dst_op.replace('%', '')

                    if src_reg in ALL_REGS and dst_reg in ALL_REGS:
                        if state.get_reg_state(dst_reg) == "ALLOCATED" and src_reg != dst_reg:
                            pass 
                        state.move_reg_to_reg(src_reg, dst_reg)

                    elif src_reg in ALL_REGS and "(" in dst_op:
                        m = re_stack.match(dst_op)
                        if m:
                            offset_str = m.group(1)
                            base_reg = m.group(2)
                            if base_reg == "rbp" or base_reg == "rsp":
                                try:
                                    offset = int(offset_str, 0)
                                    state.move_reg_to_stack(src_reg, offset)
                                except: pass

                    elif "(" in src_op and dst_reg in ALL_REGS:
                        m = re_stack.match(src_op)
                        if m:
                            offset_str = m.group(1)
                            base_reg = m.group(2)
                            if base_reg == "rbp" or base_reg == "rsp":
                                try:
                                    offset = int(offset_str, 0)
                                    state.move_stack_to_reg(offset, dst_reg)
                                except: 
                                    state.clear_reg(dst_reg)
                            else:
                                state.clear_reg(dst_reg)
                        else:
                            state.clear_reg(dst_reg)

                    elif dst_reg in ALL_REGS:
                        if src_op.startswith("$") or src_op.isdigit():
                            if state.get_reg_state(dst_reg) == "ALLOCATED":
                                origin = state.get_reg_origin(dst_reg)
                                is_whitelisted = False
                                for w in OWNERSHIP_TRANSFER_WHITELIST:
                                    if w in self.demangled_name: is_whitelisted = True; break
                                
                                if not is_whitelisted:
                                    pass
                        state.clear_reg(dst_reg)

            elif opcode in ["xor", "xorl", "xorq"]:
                ops = operands.split(',')
                if len(ops) == 2:
                    r1 = ops[0].strip().replace('%', '')
                    r2 = ops[1].strip().replace('%', '')
                    if r1 == r2 and r1 in ALL_REGS:
                        if state.get_reg_state(r1) == "ALLOCATED":
                            origin = state.get_reg_origin(r1)
                            is_whitelisted = False
                            for w in OWNERSHIP_TRANSFER_WHITELIST:
                                if w in self.demangled_name: is_whitelisted = True; break
                            
                            if not is_whitelisted:
                                pass
                        state.clear_reg(r1)

            elif opcode == "cli":
                interrupts_disabled = True
            elif opcode == "sti":
                interrupts_disabled = False
            elif opcode == "popfq" or opcode == "popf":
                interrupts_disabled = False 

            elif opcode.startswith("ret"):
                if lock_depth > 0:
                    self.issues.append(f"LOCK LEAK: Function returns with {lock_depth} locks held.")
                
                if interrupts_disabled:
                    is_safe = False
                    for safe in IRQ_SAFE_WHITELIST:
                        if safe in self.demangled_name: is_safe = True; break
                    
                    if not is_safe:
                        self.issues.append(f"IRQ LEAK: Function returns with Interrupts Disabled (CLI).")

                return

            elif opcode.startswith("jmp"):
                target_addr = 0
                try:
                    parts = operands.split()
                    target_addr = int(parts[0], 16)
                except: pass
                
                if target_addr == addr:
                    is_whitelisted = False
                    for w in LOOP_WHITELIST:
                        if w in self.demangled_name: is_whitelisted = True; break
                    if not is_whitelisted:
                        self.issues.append(f"DEAD LOOP: 0x{addr:x} -> Infinite JMP to self.")

class AuditEngine:
    def __init__(self):
        self.functions =[]

    def load(self, filename):
        if not os.path.exists(filename):
            console.print(f"[bold red]Hata:[/bold red] {filename} bulunamadı.")
            sys.exit(1)

        file_size = os.path.getsize(filename)
        re_func = re.compile(r'^([0-9a-fA-F]+) <(.*)>:$')
        re_asm = re.compile(r'^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]{2}\s+)+(.+)$')
        
        current_func = None

        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            TextColumn("{task.percentage:>3.0f}%"),
            transient=True
        ) as progress:
            task = progress.add_task(f"[cyan]Assembly Ayrıştırılıyor...", total=file_size)
            
            with open(filename, 'r', encoding='utf-8', errors='replace') as f:
                for line in f:
                    progress.update(task, advance=len(line))
                    line = line.strip()
                    if not line: continue

                    m_func = re_func.match(line)
                    if m_func:
                        addr = int(m_func.group(1), 16)
                        name = m_func.group(2)
                        current_func = FunctionAnalyzer(name, addr)
                        self.functions.append(current_func)
                        continue

                    m_asm = re_asm.match(line)
                    if m_asm and current_func:
                        addr = int(m_asm.group(1), 16)
                        asm_code = m_asm.group(3).strip()
                        current_func.add_instruction(addr, asm_code)

    def generate_report(self):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        output_file = os.path.join(script_dir, "asm.txt")
        
        try:
            with open(output_file, "w", encoding="utf-8") as f:
                f.write("MeowOS Deep Static Audit Report v8.5\n")
                f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
                f.write("====================================\n\n")
                
                total_issues = 0
                for func in self.functions:
                    if func.issues:
                        f.write(f"\nFunction: {func.demangled_name} (0x{func.addr:x})\n")
                        f.write("-" * 60 + "\n")
                        for issue in func.issues:
                            f.write(f" - {issue}\n")
                            total_issues += 1
                
                f.write("\n" + "="*36 + "\n")
                if total_issues == 0:
                    f.write("STATUS: CLEAN. No issues detected.\n")
                else:
                    f.write(f"STATUS: WARNING. {total_issues} potential issues found.\n")

            console.print(f"[bold green]Rapor dosyası oluşturuldu:[/bold green] {output_file}")

        except Exception as e:
            console.print(f"[bold red]Rapor yazılamadı:[/bold red] {e}")

    def run_audit(self):
        console.print("\n[bold cyan]=== MeowOS Derinlemesine Kod Analizi (v8.5) ===[/bold cyan]")
        console.print("Register Data Flow, Stack Tracking & Abstract Interpretation...", style="dim")
        console.print(f"Analiz edilen fonksiyon sayısı: {len(self.functions)}\n")
        
        total_issues = 0
        
        table = Table(title="Tespit Edilen Kritik Bulgular", show_lines=True)
        table.add_column("Fonksiyon", style="yellow")
        table.add_column("Hata Tipi", style="bold red")
        table.add_column("Detay", style="white")

        for func in self.functions:
            func.analyze()
            if func.issues:
                for issue in func.issues:
                    try:
                        error_type = issue.split(':')[0]
                        detail = issue.split(':', 1)[1].strip()
                    except:
                        error_type = "UNKNOWN"
                        detail = issue
                        
                    table.add_row(func.demangled_name, error_type, detail)
                    total_issues += 1

        if total_issues > 0:
            console.print(table)
            console.print(f"\n[bold red]TOPLAM {total_issues} POTANSİYEL HATA BULUNDU![/bold red]")
            console.print("[dim]Not: Statik analiz 'False Positive' üretebilir. Lütfen kodu manuel kontrol edin.[/dim]")
        else:
            console.print(Panel("[bold green]✔ SİSTEM TEMİZ[/bold green]\n"
                                "Bellek sızıntısı, kilit hatası, IRQ kaçağı veya ölü döngü tespit edilemedi.", 
                                border_style="green"))
        
        self.generate_report()

def main():
    parser = argparse.ArgumentParser(description="MeowOS Deep Static Analyzer")
    parser.add_argument("--file", help="Disassembly dosyası yolu", default=KERNEL_DIS)
    args = parser.parse_args()

    auditor = AuditEngine()
    auditor.load(args.file)
    auditor.run_audit()

if __name__ == "__main__":
    main()