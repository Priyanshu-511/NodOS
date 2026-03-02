#include "../include/shell.h"
#include "../include/gui_terminal.h"
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
#include "../include/pager.h"
#include "../include/gui_vi.h"
#include "../include/settings_config.h"

static bool s_gui_active = false;

void shell_set_gui_output(bool active) {
    s_gui_active = active;
}

bool shell_gui_output_active() {
    return s_gui_active;
}


//  Output Routing Wrappers (GUI Terminal vs VGA Text Mode)


void shell_print(const char* str) {
    if (s_gui_active) gui_terminal_print(str);
    else vga.print(str);
}

void shell_println(const char* str) {
    if (s_gui_active) {
        gui_terminal_print(str);
        gui_terminal_print("\n");
    } else {
        vga.println(str);
    }
}

void shell_putchar(char c) {
    if (s_gui_active) {
        char buf[2] = {c, '\0'};
        gui_terminal_print(buf);
    } else {
        vga.putChar(c);
    }
}

static void shell_print_uint(uint32_t n) {
    if (!s_gui_active) {
        vga.printUInt(n);
        return;
    }
    if (n == 0) {
        gui_terminal_print("0");
        return;
    }
    char buf[16];
    int i = 14;
    buf[15] = '\0';
    while (n > 0) {
        buf[i--] = (n % 10) + '0';
        n /= 10;
    }
    gui_terminal_print(&buf[i + 1]);
}

static void shell_print_int(int n) {
    if (!s_gui_active) {
        vga.printInt(n);
        return;
    }
    if (n < 0) {
        gui_terminal_print("-");
        n = -n;
    }
    shell_print_uint((uint32_t)n);
}

//  Colour helpers (Only applies to VGA text mode for now)
static void sc(VGAColor c)   { if (!s_gui_active) vga.setColor(c, BLACK); }
static void setc(VGAColor c) { if (!s_gui_active) vga.setColor(c, BLACK); }
static void rc()             { if (!s_gui_active) vga.setColor(LIGHT_GREY, BLACK); }
static void err(const char* m) { setc(LIGHT_RED); shell_println(m); rc(); }

// Helper to format the command and register it in the process table
static void record_cmd_as_process(const char* cmd, const char* arg) {
    // Ignore internal commands that shouldn't be tracked
    if (!cmd || k_strcmp(cmd, "ps") == 0 || k_strcmp(cmd, "clear") == 0 || 
        k_strcmp(cmd, "help") == 0 || k_strcmp(cmd, "kill") == 0 || 
        k_strcmp(cmd, "ls") == 0 || k_strcmp(cmd, "cd") == 0) {
        return;
    }

    char name[32];
    name[0] = '\0';

    // Format specific commands
    if (k_strcmp(cmd, "nodev") == 0) k_strncpy(name, "NodeV", 32);
    else if (k_strcmp(cmd, "write") == 0) k_strncpy(name, "Write", 32);
    else if (k_strcmp(cmd, "echo") == 0) k_strncpy(name, "Echo", 32);
    else {
        k_strncpy(name, cmd, 32);
        if (name[0] >= 'a' && name[0] <= 'z') name[0] -= 32; // Capitalize
    }

    if (arg) {
        k_strncat(name, "(", 32);
        k_strncat(name, arg, 32);
        k_strncat(name, ")", 32);
    }

    // Call the newly defined function directly!
    process_record_dummy(name);
}


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

static void join_args(char* argv[], int from, int argc, char* dest, int sz) {
    dest[0] = '\0';
    for (int i = from; i < argc; i++) {
        if (i > from) k_strncat(dest, " ", sz);
        k_strncat(dest, argv[i], sz);
    }
}


//  Commands

static void cmd_help() {
    setc(LIGHT_CYAN); shell_println("=== NodOS Shell Commands ==="); rc();
    shell_println("  help                 Show this list");
    shell_println("  fetch                Show system info banner");
    shell_println("  clear                Clear screen");
    shell_println("  info                 System information");
    shell_println("  mem                  Memory usage");
    shell_println("  time                 Uptime");
    shell_println("  ps                   List processes");
    shell_println("  kill <pid>           Kill process");
    shell_println("  echo <text>          Print text");
    shell_println("  shutdown             Shutdown system");
    shell_println("  reboot               Reboot system");
    shell_println("  halt                 Halt CPU");
    setc(LIGHT_CYAN); shell_println("--- Filesystem ---"); rc();
    shell_println("  ls                   List current directory");
    shell_println("  ls <path>            List a specific directory");
    shell_println("  cat <file>           Read file");
    shell_println("  write <file> <text>  Write text to file");
    shell_println("  rm <file>            Delete file");
    shell_println("  cd <path>            Change directory  (cd .. to go up)");
    shell_println("  pwd                  Print working directory");
    shell_println("  md <dir>             Make directory");
    shell_println("  rd <dir>             Remove directory (and contents)");
    shell_println("  mv <src> <dst>       Move file or directory");
    shell_println("  cp <src> <dst>       Copy file or directory");
    setc(LIGHT_CYAN); shell_println("--- Scripting ---"); rc();
    shell_println("  nodev <file>         Run a NodeV (.nod) script");
    shell_println("  vi <file>            Open file in vi editor");
    shell_println("  mkwal <path> <top> <bot>       Create a wallpaper file");
    shell_println("  mktml <path> <bg> <fg> <cur>   Create a terminal theme file");
}

static void cmd_clear() {
    if (s_gui_active) {
        gui_terminal_clear();
    } else {
        vga.init();
    }
}

void cmd_fetch() {
    sc(BLUE);
    shell_println("      _   _           _  ___  ____");
    sc(LIGHT_BLUE);
    shell_println("     | \\ | | ___   __| |/ _ \\/ ___|");
    sc(CYAN);
    shell_println("     |  \\| |/ _ \\ / _` | | | \\___ \\");
    sc(LIGHT_CYAN);
    shell_println("     | |\\  | (_) | (_| | |_| |___) |");
    sc(WHITE);
    shell_println("     |_| \\_|\\___/ \\__,_|\\___/|____/");
    shell_println("");
    sc(DARK_GREY);
    shell_print("  +");
    for(int i = 0; i < 66; i++) shell_putchar('-');
    shell_println("+");
    sc(DARK_GREY);
    shell_print("  |   ");
    sc(YELLOW);
    shell_print("NodOS v3.0");
    sc(DARK_GREY);
    shell_print("  //  ");
    sc(LIGHT_GREEN);
    shell_print("x86 32-bit");
    sc(DARK_GREY);
    shell_print("  //  ");
    sc(LIGHT_MAGENTA);
    shell_print("NodeV Engine");
    sc(DARK_GREY);
    shell_print("  //  ");
    sc(LIGHT_RED);
    shell_print_uint(pmm_get_total_ram_mb());
    shell_print(" MB RAM");
    sc(DARK_GREY);
    shell_print("  //  ");
    shell_print("\n  |   ");
    sc(BROWN);
    uint32_t disk_mb = vfs_get_disk_size_mb();
    uint32_t disk_gb = disk_mb / 1024;
    shell_putchar('0' + disk_gb);
    shell_print(" GB Disk");
    sc(DARK_GREY);
    shell_println("   |");
    shell_print("  +");
    for(int i = 0; i < 66; i++) shell_putchar('-');
    shell_println("+");
    shell_println("");
    sc(LIGHT_GREY);
}

static void cmd_info() {
    extern uint32_t vfs_get_used_sectors();
    extern uint32_t vfs_get_disk_size_mb();
    uint32_t total_sectors = vfs_get_disk_size_mb() * 2048;  // dynamic — matches vfs.cpp MAX_SECTORS
    uint32_t used_sectors = vfs_get_used_sectors();
    uint32_t free_sectors = total_sectors - used_sectors;
    uint32_t total_mb = (total_sectors * 512) / (1024 * 1024);
    uint32_t used_mb  = (used_sectors * 512) / (1024 * 1024);
    uint32_t free_mb  = (free_sectors * 512) / (1024 * 1024);

    sc(LIGHT_CYAN); shell_println("--- Disk Usage (ATA Primary Master) ---");
    sc(WHITE);
    setc(LIGHT_CYAN); shell_println("=== System ==="); rc();
    shell_print("  OS:    "); shell_println("NodOS v1.0 (x86 32-bit)");
    shell_print("  Boot:  "); shell_println("GRUB 2 Multiboot");
    shell_print("  VGA:   "); shell_println("80x25 text mode");
    shell_print("  RAM:   "); shell_print_uint(PMM_RAM_SIZE/1024/1024); shell_println(" MB");
    shell_print("  Up:    "); shell_print_uint(pit_uptime_s()); shell_println("s");
    shell_print("  Total Space : "); shell_print_uint(total_mb); shell_println(" MB");
    shell_print("  Used Space  : "); shell_print_uint(used_mb);  shell_println(" MB");
    shell_print("  Free Space  : "); shell_print_uint(free_mb);  shell_println(" MB");
}

static void cmd_mem() {
    uint32_t fp=pmm_free_pages(), up=pmm_used_pages(), tp=PMM_PAGE_COUNT;
    setc(LIGHT_CYAN); shell_println("=== Memory ==="); rc();
    shell_print("  PMM  used : "); shell_print_uint(up*PMM_PAGE_SIZE/1024); shell_print(" KB / ");
    shell_print_uint(tp*PMM_PAGE_SIZE/1024); shell_println(" KB");
    shell_print("  PMM  free : "); shell_print_uint(fp*PMM_PAGE_SIZE/1024); shell_println(" KB");
    shell_print("  Heap used : "); shell_print_uint(heap_used()/1024); shell_print(" KB / ");
    shell_print_uint(heap_total()/1024); shell_println(" KB");
}

static void cmd_time() {
    uint32_t sec=pit_uptime_s(), h=sec/3600, m=(sec%3600)/60, s=sec%60;
    shell_print("  Uptime: ");
    if(h<10) shell_putchar('0'); shell_print_uint(h); shell_putchar(':');
    if(m<10) shell_putchar('0'); shell_print_uint(m); shell_putchar(':');
    if(s<10) shell_putchar('0'); shell_print_uint(s); shell_putchar('\n');
    shell_print("  Ticks : "); shell_print_uint(pit_ticks()); shell_println(" (100 Hz)");
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
    
    setc(LIGHT_CYAN); 
    shell_println("PID    STATE          NAME"); 
    rc();
    
    for (int i=0;i<cnt;i++) {
        // Print PID
        shell_print_int(lst[i]->pid);
        if (lst[i]->pid < 10) shell_print("      ");
        else shell_print("     ");
        
        // Print State
        setc(lst[i]->state==PROC_RUNNING ? LIGHT_GREEN : LIGHT_GREY);
        const char* st=state_str(lst[i]->state);
        shell_print(st);
        
        // Pad spacing out to 15 characters to perfectly align the names
        for (size_t j=k_strlen(st); j<15; j++) shell_putchar(' ');
        
        // Print Name
        rc(); 
        shell_println(lst[i]->name);
    }
}

static void cmd_kill(const char* arg) {
    if (!arg) { err("Usage: kill <pid>"); return; }
    int pid=k_atoi(arg);
    if (pid<=1) { err("Cannot kill shell (PID 1)"); return; }
    if (!process_get(pid)) { err("Process not found."); return; }
    process_kill(pid);
    shell_print("Killed PID "); shell_print_int(pid); shell_putchar('\n');
}

static void cmd_echo(char* argv[], int argc) {
    for (int i=1;i<argc;i++) { if(i>1) shell_putchar(' '); shell_print(argv[i]); }
    shell_putchar('\n');
}


//  Filesystem commands


static void cmd_pwd() {
    char cwd[VFS_MAX_PATH]; vfs_getcwd(cwd, VFS_MAX_PATH);
    shell_println(cwd);
}

static void cmd_cd(const char* path) {
    if (!path) { err("Usage: cd <path>"); return; }
    if (vfs_chdir(path) < 0) {
        err("cd: No such directory");
    }
}

static void cmd_ls(const char* path) {
    char cwd[VFS_MAX_PATH];
    if (path) {
        char abs[VFS_MAX_PATH]; vfs_resolve(path, abs, VFS_MAX_PATH);
        k_strncpy(cwd, abs, VFS_MAX_PATH-1);
    } else {
        vfs_getcwd(cwd, VFS_MAX_PATH);
    }
    static char names[VFS_MAX_ENTRIES][VFS_MAX_PATH];
    static bool is_dir[VFS_MAX_ENTRIES];
    int count = 0;
    vfs_listdir(cwd, names, is_dir, &count);

    if (count == 0) { shell_println("  (empty)"); return; }
    for (int i = 0; i < count; i++) {
        if (is_dir[i]) {
            setc(LIGHT_CYAN);  shell_print("  [DIR]  "); shell_println(names[i]);
        } else {
            char fp[VFS_MAX_PATH]; k_strncpy(fp, cwd, VFS_MAX_PATH-2);
            if (fp[k_strlen(fp)-1] != '/') k_strcat(fp, "/");
            k_strncat(fp, names[i], VFS_MAX_PATH);
            setc(LIGHT_GREEN); shell_print("  [FILE] "); rc();
            shell_print(names[i]); shell_print("  (");
            shell_print_uint(vfs_size(fp)); shell_println(" bytes)");
        }
        rc();
    }
}

static void cmd_cat(const char* name) {
    if (!name) { err("Usage: cat <file>"); return; }
    static char buf[VFS_MAX_FILESIZE];
    if (vfs_read(name, buf, sizeof(buf)) < 0) { err("cat: File not found"); return; }
    shell_println(buf);
}

static void cmd_write(const char* name, char* argv[], int argc) {
    if (!name || argc < 3) { err("Usage: write <file> <text>"); return; }
    static char text[VFS_MAX_FILESIZE];
    join_args(argv, 2, argc, text, VFS_MAX_FILESIZE-1);
    uint32_t len = k_strlen(text);
    vfs_write(name, text, len);
    shell_print("Wrote "); shell_print_uint(len); shell_print(" bytes to "); shell_println(name);
}

static void cmd_rm(const char* name) {
    if (!name) { err("Usage: rm <file>"); return; }
    if (vfs_delete(name) == 0) { shell_print("Deleted: "); shell_println(name); }
    else err("rm: File not found");
}

static void cmd_md(const char* path) {
    if (!path) { err("Usage: md <path>"); return; }
    if (vfs_mkdir(path) >= 0) { shell_print("Created: "); shell_println(path); }
    else err("md: Already exists or no space");
}

static void cmd_rd(const char* path) {
    if (!path) { err("Usage: rd <path>"); return; }
    if (vfs_rmdir(path) == 0) { shell_print("Removed: "); shell_println(path); }
    else err("rd: Cannot remove (root?)");
}

static void cmd_mv(const char* src, const char* dst) {
    if (!src || !dst) { err("Usage: mv <src> <dst>"); return; }
    if (vfs_mv(src, dst) == 0) { shell_print("Moved: "); shell_print(src); shell_print(" -> "); shell_println(dst); }
    else err("mv: Failed (dest exists, or src not found)");
}

static void cmd_cp(const char* src, const char* dst) {
    if (!src || !dst) { err("Usage: cp <src> <dst>"); return; }
    if (vfs_cp(src, dst) == 0) { shell_print("Copied: "); shell_print(src); shell_print(" -> "); shell_println(dst); }
    else err("cp: Failed (dest exists, no space, or src not found)");
}

static void cmd_nodev(const char* file) {
    if (!file) { err("Usage: nodev <file.nod>"); return; }
    setc(LIGHT_CYAN);
    shell_print("[NodeV] Running: "); shell_println(file); rc();
    nodev_run_file(file);
}

static void cmd_mkwal(const char* path, const char* top, const char* bot) {
    if (!path || !top || !bot) {
        err("Usage: mkwal <path> <top_hex> <bot_hex>");
        err("  e.g: mkwal /wallpapers/aurora.wal 0A2A1A 030D08");
        return;
    }
    // Build "TOPCOLOR\nBOTCOLOR\n" — exactly what the wallpaper parser expects
    char content[20];
    int i = 0;
    for (int j = 0; top[j] && i < 8; j++) content[i++] = top[j];
    content[i++] = '\n';
    for (int j = 0; bot[j] && i < 17; j++) content[i++] = bot[j];
    content[i++] = '\n';
    content[i] = '\0';
    if (vfs_write(path, content, i) >= 0) {
        shell_print("Wallpaper written: "); shell_println(path);
    } else {
        err("mkwal: Write failed (parent directory exists?)");
    }
}


static void cmd_mktml(const char* path, const char* bg, const char* fg, const char* cursor) {
    if (!path || !bg || !fg || !cursor) {
        err("Usage: mktml <path> <bg_hex> <fg_hex> <cursor_hex>");
        err("  e.g: mktml /themes/hacker.tml 0A0F1A 00FF88 00FF88");
        return;
    }
    // Build "BG\nFG\nCURSOR\n" — three hex colour lines
    char content[30];
    int i = 0;
    for (int j = 0; bg[j]     && i < 8;  j++) content[i++] = bg[j];
    content[i++] = '\n';
    for (int j = 0; fg[j]     && i < 17; j++) content[i++] = fg[j];
    content[i++] = '\n';
    for (int j = 0; cursor[j] && i < 26; j++) content[i++] = cursor[j];
    content[i++] = '\n';
    content[i] = '\0';
    // Ensure /themes directory exists
    if (vfs_exists("/themes") < 0) vfs_mkdir("/themes");
    if (vfs_write(path, content, i) >= 0) {
        shell_print("Terminal theme written: "); shell_println(path);
    } else {
        err("mktml: Write failed (check parent directory exists)");
    }
}


//  System

static void cmd_reboot() {
    shell_println("Rebooting..."); __asm__ volatile("cli");
    uint8_t tmp; do { __asm__ volatile("inb $0x64, %0":"=a"(tmp)); } while(tmp&2);
    __asm__ volatile("outb %0, $0x64"::"a"((uint8_t)0xFE));
    while(1) __asm__ volatile("hlt");
}

static void cmd_halt() {
    setc(LIGHT_RED); shell_println("Halted. Safe to power off."); rc();
    __asm__ volatile("cli"); while(1) __asm__ volatile("hlt");
}


//  Prompt & Dispatch


static void print_prompt() {
    char cwd[VFS_MAX_PATH]; vfs_getcwd(cwd, VFS_MAX_PATH);
    setc(LIGHT_GREEN);  shell_print(g_settings.hostname);
    setc(LIGHT_GREY);   shell_print("@kernel");
    setc(DARK_GREY);    shell_print(":");
    setc(LIGHT_CYAN);   shell_print(cwd);
    setc(YELLOW);       shell_print("$ ");
    setc(WHITE);
}

void shell_exec(const char* line) {
    static char buf[256];
    k_strncpy(buf, line, 255); buf[255]='\0';
    char* argv[16]; int argc = split_args(buf, argv, 16);
    if (argc == 0) return;

    const char* a0 = argv[0];
    const char* a1 = argc>1 ? argv[1] : nullptr;
    const char* a2 = argc>2 ? argv[2] : nullptr;

    record_cmd_as_process(a0, a1);

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
    if (k_strcmp(a0,"vi")    ==0) { if(a1) { if(!s_gui_active) {vga.enable_cursor(); vi_open(a1);} else {gui_vi_open(a1);}} else err("Usage: vi <file>"); return; }
    if (k_strcmp(a0,"nodev") ==0) { cmd_nodev(a1); return; }
    if (k_strcmp(a0,"mkwal") ==0) { cmd_mkwal(a1, a2, argc>3?argv[3]:nullptr); return; }
    if (k_strcmp(a0,"mktml") ==0) { cmd_mktml(a1, a2, argc>3?argv[3]:nullptr, argc>4?argv[4]:nullptr); return; }

    setc(LIGHT_RED); shell_print("Unknown: '"); shell_print(a0); shell_println("'"); rc();
    shell_println("  Type 'help' for commands.");
}


//  Init & Main loop

void shell_init() {
    s_gui_active = false;
}
//     vfs_mkdir("/home");

//     const char* readme =
//         "========== NodOS Command Examples ==========\n\n"
//         "--- System ---\n"
//         "  help         -> Shows the short list of commands\n"
//         "  info         -> Shows OS version and details\n"
//         "  mem          -> Shows RAM usage\n\n"
//         "--- Navigation ---\n"
//         "  ls           -> Lists files in current folder\n"
//         "  ls /home     -> Lists files in the /home folder\n"
//         "  cd /home     -> Enters the /home folder\n"
//         "  cd ..        -> Goes up one folder\n"
//         "  md docs      -> Creates a new folder named 'docs'\n"
//         "  rd docs      -> Deletes the 'docs' folder\n\n"
//         "--- File Operations ---\n"
//         "  write f.txt Hello  -> Creates f.txt containing 'Hello'\n"
//         "  cat f.txt          -> Prints the contents of f.txt\n"
//         "  vi script.nod      -> Opens the text editor\n"
//         "  rm f.txt           -> Deletes f.txt\n"
//         "  cp a.txt b.txt     -> Duplicates a.txt as b.txt\n"
//         "  cp a.txt /home     -> Copies a.txt into /home\n"
//         "  mv a.txt b.txt     -> Renames a.txt to b.txt\n"
//         "  mv b.txt /home     -> Moves b.txt into /home\n\n"
//         "--- Scripting ---\n"
//         "  nodev hello.nod    -> Runs the NodeV script\n";
//     vfs_write("/readme.txt", readme, k_strlen(readme));

//     const char* script =
//         "pout(\"--- NodeV Syntax Demo ---\\n\");\n\n"
//         "// 1. Variables\n"
//         "int x = 10;\n"
//         "string name = \"NodOS\";\n"
//         "float pi = 3.14;\n"
//         "pout(\"Hello \", name, \"! x=\", x, \"\\n\");\n\n"
//         "// 2. Lists (Arrays up to 64 items)\n"
//         "list arr;\n"
//         "arr[0] = 100;\n"
//         "arr[1] = 200;\n\n"
//         "// 3. Functions\n"
//         "function add(a, b) {\n"
//         "    return a + b;\n"
//         "}\n"
//         "int sum = add(arr[0], arr[1]);\n"
//         "pout(\"Sum of array = \", sum, \"\\n\");\n\n"
//         "// 4. Loops & Conditionals\n"
//         "pout(\"Counting: \");\n"
//         "for (int i = 0, i < 3, i = i + 1) {\n"
//         "    if (i == 2) { pout(i, \"\\n\"); }\n"
//         "    else { pout(i, \", \"); }\n"
//         "}\n\n"
//         "// 5. User Input\n"
//         "pout(\"Enter your name: \");\n"
//         "string user;\n"
//         "pin(user);\n"
//         "pout(\"Nice to meet you, \", user, \"!\\n\");\n";
//     vfs_write("/hello.nod", script, k_strlen(script));

//     const char* text =
//         "You can read this file";
//     vfs_write("/sample.txt", text, k_strlen(text));
// }

void shell_run() {
    static char line[256];
    setc(LIGHT_CYAN); shell_println("\n  NodOS shell - type 'help' for commands."); rc();
    while (true) {
        print_prompt();
        if (!s_gui_active) vga.disable_cursor();
        keyboard_readline(line, sizeof(line));
        rc();
        pager_enable();
        shell_exec(line);
        pager_disable();
    }
}