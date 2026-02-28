#pragma once
#include <stdint.h>

static const uint32_t ATA_SECTOR_SIZE = 512;

void ata_init();
void ata_read_sector(uint32_t lba, uint8_t* buffer);
void ata_write_sector(uint32_t lba, const uint8_t* buffer);