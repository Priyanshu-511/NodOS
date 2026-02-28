#include "../include/shell.h"
#include "../include/vga.h"
#include "../include/vfs.h"
#include "../include/keyboard.h"
#include "../include/kstring.h"
#include "../include/pit.h"
#include "../include/pmm.h"
#include "../include/heap.h"
#include "../include/process.h"
#include "../include/nodev.h"
#include "../include/power.h"
#include "../include/vi.h"


//  Colour helpers 
static void setc(VGAColor c) { vga.setColor(c, BLACK); }
static void rc()             { vga.setColor(LIGHT_GREY, BLACK); }
static void err(const char* m) { setc(LIGHT_RED); vga.println(m); rc(); }

//  Tokeniser 
static int split_args(char* buf, char* argv[], int max) {
    int argc = 0; char* p = buf;
    while (*p && argc < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

// Join argv[from..argc-1] into dest (for multi-word text arguments)
static void join_args(char* argv[], int from, int argc, char* dest, int sz) {
    dest[0] = '\0';
    for (int i = from; i < argc; i++) {
        if (i > from) k_strncat(dest, " ", sz);
        k_strncat(dest, argv[i], sz);
    }
}

//  Commands 
static void cmd_help() {
    setc(LIGHT_CYAN); vga.println("=== NodOS Shell Commands ==="); rc();
    vga.println("  help                 Show this list");
    vga.println("  fetch                Show system info banner");
    vga.println("  clear                Clear screen");
    vga.println("  info                 System information");
    vga.println("  mem                  Memory usage");
    vga.println("  time                 Uptime");
    vga.println("  ps                   List processes");
    vga.println("  kill <pid>           Kill process");
    vga.println("  echo <text>          Print text");
    vga.println("  shutdown             Shutdown system");
    vga.println("  reboot               Reboot system");
    vga.println("  halt                 Halt CPU");
    setc(LIGHT_CYAN); vga.println("--- Filesystem ---"); rc();
    vga.println("  ls                   List current directory");
    vga.println("  ls <path>            List a specific directory");
    vga.println("  cat <file>           Read file");
    vga.println("  write <file> <text>  Write text to file");
    vga.println("  rm <file>            Delete file");
    vga.println("  cd <path>            Change directory  (cd .. to go up)");
    vga.println("  pwd                  Print working directory");
    vga.println("  md <dir>             Make directory");
    vga.println("  rd <dir>             Remove directory (and contents)");
    vga.println("  mv <src> <dst>       Move file or directory");
    vga.println("  cp <src> <dst>       Copy file or directory");
    setc(LIGHT_CYAN); vga.println("--- Scripting ---"); rc();
    vga.println("  nodev <file>         Run a NodeV (.nod) script");
    vga.println("  vi <file>            Open file in vi editor");
}

static void cmd_clear() { vga.init(); }

void cmd_fetch() {
    vga.setColor(BLUE, BLACK);
    vga.println("      _   _           _  ___  ____");
    vga.setColor(LIGHT_BLUE, BLACK);
    vga.println("     | \\ | | ___   __| |/ _ \\/ ___|");
    vga.setColor(CYAN, BLACK);
    vga.println("     |  \\| |/ _ \\ / _` | | | \\___ \\");
    vga.setColor(LIGHT_CYAN, BLACK);
    vga.println("     | |\\  | (_) | (_| | |_| |___) |");
    vga.setColor(WHITE, BLACK);
    vga.println("     |_| \\_|\\___/ \\__,_|\\___/|____/");
    vga.println("");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  +");
    for(int i = 0; i < 66; i++) vga.putChar('-');
    vga.println("+");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  |   ");
    vga.setColor(YELLOW, BLACK);
    vga.print("NodOS v3.0");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  //  ");
    vga.setColor(LIGHT_GREEN, BLACK);
    vga.print("x86 32-bit");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  //  ");
    vga.setColor(LIGHT_MAGENTA, BLACK);
    vga.print("NodeV Engine");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  //  ");
    vga.setColor(LIGHT_RED, BLACK);
    vga.printUInt(pmm_get_total_ram_mb());
    vga.print(" MB RAM");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  //  ");
    vga.print("\n  |   ");
    vga.setColor(BROWN, BLACK);
    uint32_t disk_mb = vfs_get_disk_size_mb();
    uint32_t disk_gb = disk_mb / 1024;
    vga.putChar('0' + disk_gb);
    vga.print(" GB Disk");
    vga.setColor(DARK_GREY, BLACK);
    vga.println("   |");
    vga.print("  +");
    for(int i = 0; i < 66; i++) vga.putChar('-');
    vga.println("+");
    vga.println("");
    vga.setColor(LIGHT_GREY, BLACK);
}

static void cmd_info() {
    extern uint32_t vfs_get_used_sectors();
    uint32_t total_sectors = 4194304;
    uint32_t used_sectors = vfs_get_used_sectors();
    uint32_t free_sectors = total_sectors - used_sectors;
    uint32_t total_mb = (total_sectors * 512) / (1024 * 1024);
    uint32_t used_mb  = (used_sectors * 512) / (1024 * 1024);
    uint32_t free_mb  = (free_sectors * 512) / (1024 * 1024);

    vga.setColor(LIGHT_CYAN, BLACK); vga.println("--- Disk Usage (ATA Primary Master) ---");
    vga.setColor(WHITE, BLACK);
    setc(LIGHT_CYAN); vga.println("=== System ==="); rc();
    vga.print("  OS:    "); vga.println("NodOS v1.0 (x86 32-bit)");
    vga.print("  Boot:  "); vga.println("GRUB 2 Multiboot");
    vga.print("  VGA:   "); vga.println("80x25 text mode");
    vga.print("  RAM:   "); vga.printUInt(PMM_RAM_SIZE/1024/1024); vga.println(" MB");
    vga.print("  Up:    "); vga.printUInt(pit_uptime_s()); vga.println("s");
    vga.print("  Total Space : "); vga.printUInt(total_mb); vga.println(" MB");
    vga.print("  Used Space  : "); vga.printUInt(used_mb);  vga.println(" MB");
    vga.print("  Free Space  : "); vga.printUInt(free_mb);  vga.println(" MB");
}

static void cmd_mem() {
    uint32_t fp=pmm_free_pages(), up=pmm_used_pages(), tp=PMM_PAGE_COUNT;
    setc(LIGHT_CYAN); vga.println("=== Memory ==="); rc();
    vga.print("  PMM  used : "); vga.printUInt(up*PMM_PAGE_SIZE/1024); vga.print(" KB / ");
    vga.printUInt(tp*PMM_PAGE_SIZE/1024); vga.println(" KB");
    vga.print("  PMM  free : "); vga.printUInt(fp*PMM_PAGE_SIZE/1024); vga.println(" KB");
    vga.print("  Heap used : "); vga.printUInt(heap_used()/1024); vga.print(" KB / ");
    vga.printUInt(heap_total()/1024); vga.println(" KB");
}

static void cmd_time() {
    uint32_t sec=pit_uptime_s(), h=sec/3600, m=(sec%3600)/60, s=sec%60;
    vga.print("  Uptime: ");
    if(h<10) vga.putChar('0'); vga.printUInt(h); vga.putChar(':');
    if(m<10) vga.putChar('0'); vga.printUInt(m); vga.putChar(':');
    if(s<10) vga.putChar('0'); vga.printUInt(s); vga.putChar('\n');
    vga.print("  Ticks : "); vga.printUInt(pit_ticks()); vga.println(" (100 Hz)");
}

static const char* state_str(ProcessState st) {
    if (st==PROC_READY)    return "READY";
    if (st==PROC_RUNNING)  return "RUNNING";
    if (st==PROC_SLEEPING) return "SLEEP";
    return "DEAD";
}
static void cmd_ps() {
    Process* lst[MAX_PROCESSES]; int cnt=0;
    process_list(lst, &cnt);
    setc(LIGHT_CYAN); vga.println("  PID  STATE     NAME"); rc();
    for (int i=0;i<cnt;i++) {
        vga.print("  "); vga.printInt(lst[i]->pid); vga.print("    ");
        setc(lst[i]->state==PROC_RUNNING ? LIGHT_GREEN : LIGHT_GREY);
        const char* st=state_str(lst[i]->state);
        vga.print(st);
        for (size_t j=k_strlen(st);j<10;j++) vga.putChar(' ');
        rc(); vga.println(lst[i]->name);
    }
}

static void cmd_kill(const char* arg) {
    if (!arg) { err("Usage: kill <pid>"); return; }
    int pid=k_atoi(arg);
    if (pid<=1) { err("Cannot kill shell (PID 1)"); return; }
    if (!process_get(pid)) { err("Process not found."); return; }
    process_kill(pid);
    vga.print("Killed PID "); vga.printInt(pid); vga.putChar('\n');
}

static void cmd_echo(char* argv[], int argc) {
    for (int i=1;i<argc;i++) { if(i>1) vga.putChar(' '); vga.print(argv[i]); }
    vga.putChar('\n');
}

//  Filesystem commands 

// Print current working directory
static void cmd_pwd() {
    char cwd[VFS_MAX_PATH]; vfs_getcwd(cwd, VFS_MAX_PATH);
    vga.println(cwd);
}

// Change directory
static void cmd_cd(const char* path) {
    if (!path) { err("Usage: cd <path>"); return; }
    if (vfs_chdir(path) < 0) {
        err("cd: No such directory");
    }
}

// List directory contents
static void cmd_ls(const char* path) {
    char cwd[VFS_MAX_PATH];
    if (path) {
        char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
        k_strncpy(cwd, abs, VFS_MAX_PATH-1);
    } else {
        vfs_getcwd(cwd, VFS_MAX_PATH);
    }
    char names[VFS_MAX_ENTRIES][VFS_MAX_PATH];
    bool is_dir[VFS_MAX_ENTRIES];
    int count = 0;
    vfs_listdir(cwd, names, is_dir, &count);

    if (count == 0) { vga.println("  (empty)"); return; }
    for (int i = 0; i < count; i++) {
        if (is_dir[i]) {
            setc(LIGHT_CYAN);  vga.print("  [DIR]  "); vga.println(names[i]);
        } else {
            // Build full path to get size
            char fp[VFS_MAX_PATH]; k_strncpy(fp, cwd, VFS_MAX_PATH-2);
            if (fp[k_strlen(fp)-1] != '/') k_strcat(fp, "/");
            k_strncat(fp, names[i], VFS_MAX_PATH);
            setc(LIGHT_GREEN); vga.print("  [FILE] "); rc();
            vga.print(names[i]); vga.print("  (");
            vga.printUInt(vfs_size(fp)); vga.println(" bytes)");
        }
        rc();
    }
}

// Read file
static void cmd_cat(const char* name) {
    if (!name) { err("Usage: cat <file>"); return; }
    static char buf[VFS_MAX_FILESIZE];
    if (vfs_read(name, buf, sizeof(buf)) < 0) { err("cat: File not found"); return; }
    vga.println(buf);
}

// Write file (create or overwrite)
static void cmd_write(const char* name, char* argv[], int argc) {
    if (!name || argc < 3) { err("Usage: write <file> <text>"); return; }
    static char text[VFS_MAX_FILESIZE];
    join_args(argv, 2, argc, text, VFS_MAX_FILESIZE-1);
    uint32_t len = k_strlen(text);
    vfs_write(name, text, len);
    vga.print("Wrote "); vga.printUInt(len); vga.print(" bytes to "); vga.println(name);
}

// Delete file
static void cmd_rm(const char* name) {
    if (!name) { err("Usage: rm <file>"); return; }
    if (vfs_delete(name) == 0) { vga.print("Deleted: "); vga.println(name); }
    else err("rm: File not found");
}

// Make directory
static void cmd_md(const char* path) {
    if (!path) { err("Usage: md <path>"); return; }
    if (vfs_mkdir(path) >= 0) { vga.print("Created: "); vga.println(path); }
    else err("md: Already exists or no space");
}

// Remove directory
static void cmd_rd(const char* path) {
    if (!path) { err("Usage: rd <path>"); return; }
    if (vfs_rmdir(path) == 0) { vga.print("Removed: "); vga.println(path); }
    else err("rd: Cannot remove (root?)");
}

// Move / rename
static void cmd_mv(const char* src, const char* dst) {
    if (!src || !dst) { err("Usage: mv <src> <dst>"); return; }
    if (vfs_mv(src, dst) == 0) { vga.print("Moved: "); vga.print(src); vga.print(" -> "); vga.println(dst); }
    else err("mv: Failed (dest exists, or src not found)");
}

// Copy
static void cmd_cp(const char* src, const char* dst) {
    if (!src || !dst) { err("Usage: cp <src> <dst>"); return; }
    if (vfs_cp(src, dst) == 0) { vga.print("Copied: "); vga.print(src); vga.print(" -> "); vga.println(dst); }
    else err("cp: Failed (dest exists, no space, or src not found)");
}

// Run NodeV script
static void cmd_nodev(const char* file) {
    if (!file) { err("Usage: nodev <file.nod>"); return; }
    setc(LIGHT_CYAN);
    vga.print("[NodeV] Running: "); vga.println(file); rc();
    nodev_run_file(file);
}

// System
static void cmd_reboot() {
    vga.println("Rebooting..."); __asm__ volatile("cli");
    uint8_t tmp; do { __asm__ volatile("inb $0x64, %0":"=a"(tmp)); } while(tmp&2);
    __asm__ volatile("outb %0, $0x64"::"a"((uint8_t)0xFE));
    while(1) __asm__ volatile("hlt");
}
static void cmd_halt() {
    setc(LIGHT_RED); vga.println("Halted. Safe to power off."); rc();
    __asm__ volatile("cli"); while(1) __asm__ volatile("hlt");
}

//  Prompt 
static void print_prompt() {
    char cwd[VFS_MAX_PATH]; vfs_getcwd(cwd, VFS_MAX_PATH);
    setc(LIGHT_GREEN);  vga.print("nodos");
    setc(LIGHT_GREY);   vga.print("@kernel");
    setc(DARK_GREY);    vga.print(":");
    setc(LIGHT_CYAN);   vga.print(cwd);
    setc(YELLOW);       vga.print("$ ");
    setc(WHITE);
}

//  Dispatcher 
void shell_exec(const char* line) {
    static char buf[256];
    k_strncpy(buf, line, 255); buf[255]='\0';
    char* argv[16]; int argc = split_args(buf, argv, 16);
    if (argc == 0) return;

    const char* a0 = argv[0];
    const char* a1 = argc>1 ? argv[1] : nullptr;
    const char* a2 = argc>2 ? argv[2] : nullptr;

    if (k_strcmp(a0,"help")  ==0) { cmd_help();  return; }
    if (k_strcmp(a0, "fetch") == 0) { cmd_fetch(); return; }
    if (k_strcmp(a0,"clear") ==0) { cmd_clear(); return; }
    if (k_strcmp(a0,"info")  ==0) { cmd_info();  return; }
    if (k_strcmp(a0,"mem")   ==0) { cmd_mem();   return; }
    if (k_strcmp(a0,"time")  ==0) { cmd_time();  return; }
    if (k_strcmp(a0,"ps")    ==0) { cmd_ps();    return; }
    if (k_strcmp(a0,"pwd")   ==0) { cmd_pwd();   return; }
    if (k_strcmp(a0, "shutdown") == 0) { system_shutdown(); return; }
    if (k_strcmp(a0,"reboot")==0) { cmd_reboot();return; }
    if (k_strcmp(a0,"halt")  ==0) { cmd_halt();  return; }
    if (k_strcmp(a0,"echo")  ==0) { cmd_echo(argv, argc); return; }
    if (k_strcmp(a0,"kill")  ==0) { cmd_kill(a1);  return; }
    if (k_strcmp(a0,"cat")   ==0) { cmd_cat(a1);   return; }
    if (k_strcmp(a0,"rm")    ==0) { cmd_rm(a1);    return; }
    if (k_strcmp(a0,"cd")    ==0) { cmd_cd(a1);    return; }
    if (k_strcmp(a0,"md")    ==0) { cmd_md(a1);    return; }
    if (k_strcmp(a0,"rd")    ==0) { cmd_rd(a1);    return; }
    if (k_strcmp(a0,"mv")    ==0) { cmd_mv(a1,a2); return; }
    if (k_strcmp(a0,"cp")    ==0) { cmd_cp(a1,a2); return; }
    if (k_strcmp(a0,"ls")    ==0) { cmd_ls(a1);    return; }
    if (k_strcmp(a0,"write") ==0) { cmd_write(a1, argv, argc); return; }
    if (k_strcmp(a0,"vi")    ==0) { if(a1) vi_open(a1); else err("Usage: vi <file>"); return; }
    if (k_strcmp(a0,"nodev") ==0) { cmd_nodev(a1); return; }

    setc(LIGHT_RED); vga.print("Unknown: '"); vga.print(a0); vga.println("'"); rc();
    vga.println("  Type 'help' for commands.");
}

//  Init & main loop 
void shell_init() {
    // Pre-create /home directory
    vfs_mkdir("/home");

    // Preload a sample readme and hello script
    const char* readme =
        "Welcome to NodOS!\n"
        "Commands: help, info, mem, ls, cd, md, rd, mv, cp, cat, write, rm, nodev\n"
        "NodeV scripts: write hello.nod then run: nodev hello.nod\n";
    vfs_write("/readme.txt", readme, k_strlen(readme));

    const char* script =
        "pout(\"Hello from NodeV!\\n\");\n"
        "int x = 5;\n"
        "while (x > 0) {\n"
        "    pout(x, \" \");\n"
        "    x = x - 1;\n"
        "}\n"
        "pout(\"\\nDone.\\n\");\n";
    vfs_write("/hello.nod", script, k_strlen(script));
}

void shell_run() {
    static char line[256];
    setc(LIGHT_CYAN); vga.println("\n  NodOS shell - type 'help' for commands."); rc();
    while (true) {
        print_prompt();
        keyboard_readline(line, sizeof(line));
        rc();
        shell_exec(line);
    }
}
