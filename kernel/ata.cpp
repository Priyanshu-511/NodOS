#include "../include/ata.h"
#include "../include/io.h"
#include "../include/vga.h"

extern VGADriver vga;

#define ATA_PORT_DATA       0x1F0
#define ATA_PORT_SECT_CNT   0x1F2
#define ATA_PORT_LBA_LO     0x1F3
#define ATA_PORT_LBA_MID    0x1F4
#define ATA_PORT_LBA_HI     0x1F5
#define ATA_PORT_DRV        0x1F6
#define ATA_PORT_CMD        0x1F7
#define ATA_PORT_STATUS     0x1F7

#define ATA_SR_BSY          0x80
#define ATA_SR_DRQ          0x08

static void ata_wait_bsy() { while (inb(ATA_PORT_STATUS) & ATA_SR_BSY); }
static void ata_wait_drq() { while (!(inb(ATA_PORT_STATUS) & ATA_SR_DRQ)); }

void ata_init() {
    vga.setColor(LIGHT_GREEN, BLACK); vga.print("  [ OK ] ");
    vga.setColor(LIGHT_GREY,  BLACK); vga.println("ATA     (Primary Master, PIO Mode)");
}

void ata_read_sector(uint32_t lba, uint8_t* buffer) {
    ata_wait_bsy();
    outb(ATA_PORT_DRV, 0xE0 | ((lba >> 24) & 0x0F)); 
    outb(ATA_PORT_SECT_CNT, 1);
    outb(ATA_PORT_LBA_LO, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PORT_CMD, 0x20);

    ata_wait_bsy(); ata_wait_drq();
    uint16_t* ptr = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) ptr[i] = inw(ATA_PORT_DATA);
}

void ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    ata_wait_bsy();
    outb(ATA_PORT_DRV, 0xE0 | ((lba >> 24) & 0x0F)); 
    outb(ATA_PORT_SECT_CNT, 1);
    outb(ATA_PORT_LBA_LO, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PORT_CMD, 0x30);

    ata_wait_bsy(); ata_wait_drq();
    const uint16_t* ptr = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) outw(ATA_PORT_DATA, ptr[i]);
    outb(ATA_PORT_CMD, 0xE7); // Flush
    ata_wait_bsy();
}