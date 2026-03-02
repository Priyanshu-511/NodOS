#pragma once
#include <stdint.h>

// ATA driver interface (PIO mode)
// Sector size is fixed to 512 bytes (standard for ATA disks)
static const uint32_t ATA_SECTOR_SIZE = 512;

// Initialize ATA controller and detect primary drive
void ata_init();

// Read one 512-byte sector from disk at given LBA into buffer
void ata_read_sector(uint32_t lba, uint8_t* buffer);

// Write one 512-byte sector to disk at given LBA from buffer
void ata_write_sector(uint32_t lba, const uint8_t* buffer);