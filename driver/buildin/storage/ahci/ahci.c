#include "ahci.h"

#include "../../../../arch/x86_64/pci/pci.h"

#include <stdbool.h>
#include <stdint.h>

#define AHCI_CLASS_MASS_STORAGE 0x01
#define AHCI_SUBCLASS_SATA 0x06
#define AHCI_PROGIF_AHCI 0x01

#define AHCI_PORT_DET_PRESENT 0x03
#define AHCI_PORT_IPM_ACTIVE 0x01
#define AHCI_SIG_ATA 0x00000101
#define AHCI_SIG_ATAPI 0xEB140101
#define AHCI_SIG_SEMB 0xC33C0101
#define AHCI_SIG_PM 0x96690101

typedef volatile struct hba_port
{
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} hba_port_t;

typedef volatile struct hba_mem
{
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t reserved[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    hba_port_t ports[32];
} hba_mem_t;

static uint32_t sata_ports_found;

static bool ahci_port_has_sata_drive(hba_port_t *port)
{
    uint32_t ssts = port->ssts;
    uint8_t det = (uint8_t)(ssts & 0x0F);
    uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);

    if (det != AHCI_PORT_DET_PRESENT || ipm != AHCI_PORT_IPM_ACTIVE)
        return false;

    return port->sig == AHCI_SIG_ATA;
}

static void ahci_scan_pci_device(const pci_device_t *dev, void *ctx)
{
    (void)ctx;

    if (dev->class_code != AHCI_CLASS_MASS_STORAGE ||
        dev->subclass != AHCI_SUBCLASS_SATA ||
        dev->prog_if != AHCI_PROGIF_AHCI)
        return;

    uint32_t abar = pci_bar(dev, 5) & 0xFFFFFFF0u;

    if (abar == 0)
        return;

    // Early boot currently identity maps the first GiB only.
    if (abar >= 0x40000000u)
        return;

    hba_mem_t *hba = (hba_mem_t *)(uintptr_t)abar;
    uint32_t implemented = hba->pi;

    for (uint32_t i = 0; i < 32; i++)
    {
        if (!(implemented & (1u << i)))
            continue;

        if (ahci_port_has_sata_drive((hba_port_t *)&hba->ports[i]))
            sata_ports_found++;
    }
}

uint32_t ahci_init(void)
{
    sata_ports_found = 0;
    pci_scan(ahci_scan_pci_device, 0);
    return sata_ports_found;
}
