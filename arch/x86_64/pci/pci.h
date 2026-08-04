#ifndef ARCH_X86_64_PCI_H
#define ARCH_X86_64_PCI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct pci_device
{
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
} pci_device_t;

typedef void (*pci_scan_callback_t)(const pci_device_t *dev, void *ctx);

#ifdef __cplusplus
extern "C" {
#endif

uint32_t pci_config_read32(uint8_t bus,
                           uint8_t slot,
                           uint8_t function,
                           uint8_t offset);
void pci_config_write32(uint8_t bus,
                        uint8_t slot,
                        uint8_t function,
                        uint8_t offset,
                        uint32_t value);
uint16_t pci_config_read16(uint8_t bus,
                           uint8_t slot,
                           uint8_t function,
                           uint8_t offset);
uint8_t pci_config_read8(uint8_t bus,
                         uint8_t slot,
                         uint8_t function,
                         uint8_t offset);
uint32_t pci_bar(const pci_device_t *dev, uint8_t bar);
void pci_scan(pci_scan_callback_t callback, void *ctx);

#ifdef __cplusplus
}
#endif

#endif // ARCH_X86_64_PCI_H
