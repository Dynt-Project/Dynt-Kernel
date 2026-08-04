#include "pci.h"

#include "../io/io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_address(uint8_t bus,
                            uint8_t slot,
                            uint8_t function,
                            uint8_t offset)
{
    return 0x80000000u |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           (offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus,
                           uint8_t slot,
                           uint8_t function,
                           uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus,
                        uint8_t slot,
                        uint8_t function,
                        uint8_t offset,
                        uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_config_read16(uint8_t bus,
                           uint8_t slot,
                           uint8_t function,
                           uint8_t offset)
{
    uint32_t value = pci_config_read32(bus, slot, function, offset);
    return (uint16_t)(value >> ((offset & 2) * 8));
}

uint8_t pci_config_read8(uint8_t bus,
                         uint8_t slot,
                         uint8_t function,
                         uint8_t offset)
{
    uint32_t value = pci_config_read32(bus, slot, function, offset);
    return (uint8_t)(value >> ((offset & 3) * 8));
}

uint32_t pci_bar(const pci_device_t *dev, uint8_t bar)
{
    if (!dev || bar >= 6)
        return 0;

    return pci_config_read32(dev->bus, dev->slot, dev->function, (uint8_t)(0x10 + bar * 4));
}

static bool pci_read_device(uint8_t bus,
                            uint8_t slot,
                            uint8_t function,
                            pci_device_t *out)
{
    uint16_t vendor = pci_config_read16(bus, slot, function, 0x00);

    if (vendor == 0xFFFF)
        return false;

    out->bus = bus;
    out->slot = slot;
    out->function = function;
    out->vendor_id = vendor;
    out->device_id = pci_config_read16(bus, slot, function, 0x02);
    out->revision = pci_config_read8(bus, slot, function, 0x08);
    out->prog_if = pci_config_read8(bus, slot, function, 0x09);
    out->subclass = pci_config_read8(bus, slot, function, 0x0A);
    out->class_code = pci_config_read8(bus, slot, function, 0x0B);
    out->header_type = pci_config_read8(bus, slot, function, 0x0E);
    return true;
}

void pci_scan(pci_scan_callback_t callback, void *ctx)
{
    if (!callback)
        return;

    for (uint16_t bus = 0; bus < 256; bus++)
    {
        for (uint8_t slot = 0; slot < 32; slot++)
        {
            pci_device_t dev;

            if (!pci_read_device((uint8_t)bus, slot, 0, &dev))
                continue;

            uint8_t functions = (dev.header_type & 0x80) ? 8 : 1;

            for (uint8_t function = 0; function < functions; function++)
            {
                if (function != 0 &&
                    !pci_read_device((uint8_t)bus, slot, function, &dev))
                    continue;

                callback(&dev, ctx);
            }
        }
    }
}
