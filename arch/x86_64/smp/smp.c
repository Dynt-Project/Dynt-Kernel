#include "smp.h"

#include "../cpu/cpu.h"
#include "../cpu/msr.h"
#include "../cpu/percpu.h"
#include "../io/io.h"
#include "../inter/gdt.h"
#include "../inter/idt.h"
#include "../syscall/syscall.h"
#include "lapic.h"
#include "../../../init/debug.h"
#include "../../../mem/lib/memory.h"
#include "../../../mem/mm/kheap.h"
#include "../../../mem/mm/paging.h"
#include "../../../scheduler/scheduler.h"

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_ENABLE 0x800

/* ---- Intel MP Floating Pointer (legacy, fallback) ---- */
#define MP_FLOATING_SIGNATURE 0x5F504D5F
#define MP_CONFIG_SIGNATURE 0x504D4350

/* ---- ACPI MADT Local APIC type ---- */
#define ACPI_MADT_TYPE_LOCAL_APIC 0

#pragma pack(push, 1)
typedef struct mp_floating
{
    uint32_t signature;
    uint32_t config_table;
    uint8_t length;
    uint8_t spec_rev;
    uint8_t checksum;
    uint8_t feature[5];
} mp_floating_t;

typedef struct mp_config_header
{
    uint32_t signature;
    uint16_t base_table_length;
    uint8_t spec_rev;
    uint8_t checksum;
    char oem_id[8];
    char product_id[12];
    uint32_t oem_table;
    uint16_t oem_table_size;
    uint16_t entry_count;
    uint32_t lapic_address;
    uint16_t extended_table_length;
    uint8_t extended_table_checksum;
    uint8_t reserved;
} mp_config_header_t;

typedef struct mp_processor_entry
{
    uint8_t type;
    uint8_t local_apic_id;
    uint8_t local_apic_version;
    uint8_t flags;
    uint32_t signature;
    uint32_t feature_flags;
    uint32_t reserved[2];
} mp_processor_entry_t;

/* ACPI table header (common to all ACPI tables) */
typedef struct acpi_header
{
    uint8_t  signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_header_t;

/* RSDP (Root System Description Pointer) */
typedef struct acpi_rsdp
{
    char     signature[8];   /* "RSD PTR " */
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;       /* 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    uint32_t rsdt_address;   /* 32-bit RSDT address (ACPI 1.0) */
    uint32_t length;         /* Length of entire RSDP (ACPI 2.0+) */
    uint64_t xsdt_address;  /* 64-bit XSDT address (ACPI 2.0+) */
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} acpi_rsdp_t;

/* MADT Interrupt Controller Structure: Local APIC */
typedef struct acpi_madt_lapic
{
    uint8_t  type;           /* 0 */
    uint8_t  length;         /* 8 */
    uint8_t  processor_id;  /* ACPI processor UID */
    uint8_t  apic_id;       /* Local APIC ID */
    uint32_t flags;         /* bit 0 = enabled */
} acpi_madt_lapic_t;

/* MADT header (after the common ACPI header) */
typedef struct acpi_madt
{
    acpi_header_t header;
    uint32_t local_apic_addr;
    uint32_t flags;
    /* followed by Interrupt Controller Structures */
} acpi_madt_t;
#pragma pack(pop)

static smp_cpu_t cpus[SMP_MAX_BOOT_CPUS];
static uint32_t cpu_count;
static uintptr_t lapic_base;

static uint8_t checksum8(const void *ptr, uint32_t size)
{
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0;

    for (uint32_t i = 0; i < size; i++)
        sum = (uint8_t)(sum + bytes[i]);

    return sum;
}

static uint16_t lowmem_read16(uintptr_t address)
{
    uint16_t value;
    __asm__ volatile ("movw (%1), %0"
                      : "=r"(value)
                      : "r"(address)
                      : "memory");
    return value;
}

static void add_cpu(uint8_t processor_id,
                    uint8_t lapic_id,
                    bool enabled,
                    bool bootstrap)
{
    if (cpu_count >= SMP_MAX_BOOT_CPUS)
        return;

    cpus[cpu_count].processor_id = processor_id;
    cpus[cpu_count].lapic_id = lapic_id;
    cpus[cpu_count].enabled = enabled;
    cpus[cpu_count].bootstrap = bootstrap;
    cpu_count++;
}

/* ---- MP table parsing (legacy fallback) ---- */

static const mp_floating_t *scan_mp_range(uintptr_t start, uintptr_t end)
{
    for (uintptr_t ptr = start; ptr + sizeof(mp_floating_t) <= end; ptr += 16)
    {
        const mp_floating_t *mp = (const mp_floating_t *)ptr;

        if (mp->signature == MP_FLOATING_SIGNATURE &&
            mp->length == 1 &&
            checksum8(mp, 16) == 0)
            return mp;
    }

    return 0;
}

static const mp_floating_t *find_mp_table(void)
{
    uint16_t ebda_segment = lowmem_read16(0x40E);
    uintptr_t ebda = (uintptr_t)ebda_segment << 4;

    if (ebda)
    {
        const mp_floating_t *mp = scan_mp_range(ebda, ebda + 1024);
        if (mp)
            return mp;
    }

    uint16_t base_kb = lowmem_read16(0x413);
    uintptr_t top = (uintptr_t)base_kb * 1024;
    const mp_floating_t *mp = scan_mp_range(top - 1024, top);

    if (mp)
        return mp;

    return scan_mp_range(0xF0000, 0x100000);
}

static void parse_mp_config(const mp_config_header_t *cfg)
{
    if (!cfg || cfg->signature != MP_CONFIG_SIGNATURE)
        return;

    if (checksum8(cfg, cfg->base_table_length) != 0)
        return;

    lapic_base = cfg->lapic_address;
    const uint8_t *entry = (const uint8_t *)(cfg + 1);

    for (uint16_t i = 0; i < cfg->entry_count; i++)
    {
        switch (entry[0])
        {
            case 0:
            {
                const mp_processor_entry_t *cpu = (const mp_processor_entry_t *)entry;
                add_cpu(cpu->local_apic_id,
                        cpu->local_apic_id,
                        (cpu->flags & 0x01) != 0,
                        (cpu->flags & 0x02) != 0);
                entry += 20;
                break;
            }
            case 1:
            case 2:
            case 3:
            case 4:
                entry += 8;
                break;
            default:
                return;
        }
    }
}

/* ---- ACPI RSDP / MADT parsing ---- */

static const acpi_rsdp_t *scan_rsdp_range(uintptr_t start, uintptr_t end)
{
    for (uintptr_t ptr = start; ptr + sizeof(acpi_rsdp_t) <= end; ptr += 16)
    {
        const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)ptr;

        /* Check signature "RSD PTR " */
        if (k_memcmp(rsdp->signature, "RSD PTR ", 8) != 0)
            continue;

        /* Validate ACPI 1.0 checksum (first 20 bytes) */
        if (checksum8(rsdp, 20) != 0)
            continue;

        /* For ACPI 2.0+, validate extended checksum */
        if (rsdp->revision >= 2 && rsdp->length >= 36)
        {
            if (checksum8(rsdp, rsdp->length) != 0)
                continue;
        }

        return rsdp;
    }

    return 0;
}

static const acpi_rsdp_t *find_rsdp(void)
{
    /* 1. Scan EBDA */
    uint16_t ebda_segment = lowmem_read16(0x40E);
    uintptr_t ebda = (uintptr_t)ebda_segment << 4;

    if (ebda)
    {
        const acpi_rsdp_t *rsdp = scan_rsdp_range(ebda, ebda + 1024);
        if (rsdp)
            return rsdp;
    }

    /* 2. Scan BIOS ROM area 0xE0000 - 0xFFFFF */
    return scan_rsdp_range(0xE0000, 0x100000);
}

static bool acpi_validate_table(const acpi_header_t *header)
{
    if (!header || header->length < sizeof(acpi_header_t))
        return false;

    return checksum8(header, header->length) == 0;
}

static const acpi_madt_t *find_madt(const acpi_rsdp_t *rsdp)
{
    if (!rsdp)
        return 0;

    /* Try XSDT first (ACPI 2.0+) */
    if (rsdp->revision >= 2 && rsdp->xsdt_address)
    {
        const acpi_header_t *xsdt = (const acpi_header_t *)rsdp->xsdt_address;

        if (k_memcmp(xsdt->signature, "XSDT", 4) == 0 && acpi_validate_table(xsdt))
        {
            uint32_t entry_count = (xsdt->length - sizeof(acpi_header_t)) / 8;
            const uint64_t *entries = (const uint64_t *)((uintptr_t)xsdt + sizeof(acpi_header_t));

            for (uint32_t i = 0; i < entry_count; i++)
            {
                const acpi_header_t *table = (const acpi_header_t *)entries[i];
                if (table && k_memcmp(table->signature, "APIC", 4) == 0)
                    return (const acpi_madt_t *)table;
            }
        }
    }

    /* Fall back to RSDT (ACPI 1.0) */
    if (rsdp->rsdt_address)
    {
        const acpi_header_t *rsdt = (const acpi_header_t *)(uintptr_t)rsdp->rsdt_address;

        if (k_memcmp(rsdt->signature, "RSDT", 4) == 0 && acpi_validate_table(rsdt))
        {
            uint32_t entry_count = (rsdt->length - sizeof(acpi_header_t)) / 4;
            const uint32_t *entries = (const uint32_t *)((uintptr_t)rsdt + sizeof(acpi_header_t));

            for (uint32_t i = 0; i < entry_count; i++)
            {
                const acpi_header_t *table = (const acpi_header_t *)(uintptr_t)entries[i];
                if (table && k_memcmp(table->signature, "APIC", 4) == 0)
                    return (const acpi_madt_t *)table;
            }
        }
    }

    return 0;
}

static void parse_madt(const acpi_madt_t *madt)
{
    if (!madt)
        return;

    lapic_base = madt->local_apic_addr;

    /* Walk the Interrupt Controller Structures */
    uintptr_t ptr = (uintptr_t)madt + sizeof(acpi_madt_t);
    uintptr_t end = (uintptr_t)madt + madt->header.length;

    while (ptr + 2 <= end)
    {
        uint8_t type = *(const uint8_t *)ptr;
        uint8_t len = *(const uint8_t *)(ptr + 1);

        if (len < 2 || ptr + len > end)
            break;

        if (type == ACPI_MADT_TYPE_LOCAL_APIC && len >= sizeof(acpi_madt_lapic_t))
        {
            const acpi_madt_lapic_t *lapic = (const acpi_madt_lapic_t *)ptr;
            bool enabled = (lapic->flags & 0x01) != 0;

            /* First enabled CPU is the BSP */
            add_cpu(lapic->processor_id, lapic->apic_id, enabled,
                    enabled && cpu_count == 0);
        }

        ptr += len;
    }
}

void smp_init(void)
{
    k_memset(cpus, 0, sizeof(cpus));
    cpu_count = 0;
    lapic_base = (uintptr_t)(rdmsr(IA32_APIC_BASE_MSR) & 0xFFFFF000ULL);
    wrmsr(IA32_APIC_BASE_MSR, rdmsr(IA32_APIC_BASE_MSR) | IA32_APIC_BASE_ENABLE);

    /* Primary: ACPI RSDP -> MADT */
    const acpi_rsdp_t *rsdp = find_rsdp();

    if (rsdp)
    {
        const acpi_madt_t *madt = find_madt(rsdp);
        if (madt)
            parse_madt(madt);
    }

    /* Fallback: Intel MP table */
    if (cpu_count == 0)
    {
        const mp_floating_t *mp = find_mp_table();

        if (mp && mp->config_table)
            parse_mp_config((const mp_config_header_t *)(uintptr_t)mp->config_table);
    }

    /* Last resort: single BSP */
    if (cpu_count == 0)
        add_cpu(0, 0, true, true);
}

uint32_t smp_cpu_count(void)
{
    return cpu_count;
}

const smp_cpu_t *smp_cpu_at(uint32_t index)
{
    return index < cpu_count ? &cpus[index] : 0;
}

uintptr_t smp_lapic_base(void)
{
    return lapic_base;
}

/* =================== application processor boot =================== */

// runtime address of the trampoline (copied below the 1 MiB kernel image;
// the region 0x8000-0x8FFF is inside the pmm-reserved low memory).  The
// offsets must match trampoline.S.
#define TRAMP_BASE 0x8000
#define TRAMP_INFO_OFF 0x200       // trampoline_info block
#define TRAMP_INFO_CR3 0x08        //  kernel_cr3
#define TRAMP_INFO_ENTRY 0x10      //  ap_entry_64
#define TRAMP_CPU_TABLE_OFF 0x240  // 16 x 8-byte slots (stack, lapic id)
#define TRAMP_MAX_SLOTS 16

#define TRAMP_SIPI_VECTOR (TRAMP_BASE >> 12)

// kernel symbols around the trampoline blob (trampoline.S)
extern "C" char _trampoline_start[];
extern "C" char _trampoline_end[];

// lidt helper from tables_asm.S (the APs share the BSP's IDT)
extern "C" void idt_flush(uint64_t idtr_addr);

// per-cpu boot context, allocated by the BSP before the IPIs go out
typedef struct ap_boot_context
{
    uint64_t kernel_stack_top;   // interrupt stack / TSS rsp0 / boot rsp
    uint64_t syscall_stack_top;  // syscall kernel stack (percpu kernel_rsp)
    gdt_entry_t *gdt;
    tss_t *tss;
} ap_boot_context_t;

static ap_boot_context_t ap_ctx[SMP_MAX_BOOT_CPUS];
static volatile uint32_t g_ap_online;
static uint32_t g_lapic_timer_count;

// 10 ms per PIT tick; used for the INIT -> SIPI gap
static void ap_delay_ms(uint32_t ms)
{
    uint64_t start = scheduler_ticks();
    uint64_t ticks = (ms + 9) / 10;

    while (scheduler_ticks() - start < ticks)
        pause_cpu();
}

// rough busy-wait for the ~200 us gap between the two SIPIs
static void ap_delay_us(uint32_t us)
{
    uint32_t i;

    for (i = 0; i < us * 20; i++)
        pause_cpu();
}

// C entry of the application processors (trampoline jumps here with the
// cpu index in edi and the kernel stack already loaded)
extern "C" void smp_ap_entry(uint32_t cpu)
{
    if (cpu < SMP_MAX_BOOT_CPUS)
    {
        const smp_cpu_t *c = smp_cpu_at(cpu);

        if (c && ap_ctx[cpu].gdt)
        {
            // GS MSRs first: everything below resolves per-cpu state
            percpu_init_cpu(cpu, c->lapic_id, ap_ctx[cpu].kernel_stack_top,
                            ap_ctx[cpu].syscall_stack_top);

            // per-cpu GDT + TSS (TSS rsp0 = this cpu's interrupt stack)
            gdt_load_cpu(ap_ctx[cpu].gdt);

            // load the shared IDT (each cpu has its own IDTR)
            gdt_ptr_t idtr;
            uint64_t base;
            uint16_t limit;

            idt_get_ptr(&base, &limit);
            idtr.limit = limit;
            idtr.base = base;
            idt_flush((uint64_t)&idtr);

            // per-cpu syscall MSRs (STAR/LSTAR/FMASK) + clean FS base
            syscall_init();
            wrmsr(MSR_IA32_FS_BASE, 0);

            // this cpu's own 100 Hz scheduler clock
            lapic_timer_init(g_lapic_timer_count);

            __atomic_add_fetch(&g_ap_online, 1, __ATOMIC_RELEASE);

            debug_printf("[boot] cpu %u online (lapic 0x%x)\n",
                         (unsigned)cpu, (unsigned)c->lapic_id);

            sti();
            scheduler_idle_cpu();
        }
    }

    for (;;)
    {
        cli();
        hlt();
    }
}

void smp_start_aps(uint32_t lapic_timer_count)
{
    uint32_t count = smp_cpu_count();
    uint32_t expected = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        const smp_cpu_t *c = smp_cpu_at(i);

        if (c->enabled && !c->bootstrap)
            expected++;
    }

    if (expected == 0)
        return;

    g_lapic_timer_count = lapic_timer_count;
    g_ap_online = 0;

    // copy the trampoline blob to its fixed low-memory address
    uintptr_t blob = (uintptr_t)_trampoline_start;
    uintptr_t blob_end = (uintptr_t)_trampoline_end;
    size_t blob_size = blob_end > blob ? blob_end - blob : 0;

    if (blob_size == 0 ||
        blob_size > TRAMP_CPU_TABLE_OFF + TRAMP_MAX_SLOTS * 8)
    {
        debug_printf("[boot] smp: bad trampoline size %lu\n",
                     (unsigned long)blob_size);
        return;
    }

    k_memcpy((void *)TRAMP_BASE, (const void *)blob, blob_size);

    // shared info block: cr3 + C entry (cpu-independent)
    *(volatile uint64_t *)(TRAMP_BASE + TRAMP_INFO_OFF + TRAMP_INFO_CR3) =
        paging_kernel_cr3();
    *(volatile uint64_t *)(TRAMP_BASE + TRAMP_INFO_OFF + TRAMP_INFO_ENTRY) =
        (uint64_t)(uintptr_t)smp_ap_entry;

    // per-cpu stacks + GDT/TSS, and the slot table the APs search by lapic id
    for (uint32_t i = 0; i < count && i < TRAMP_MAX_SLOTS; i++)
    {
        const smp_cpu_t *c = smp_cpu_at(i);

        uintptr_t kstack = (uintptr_t)kheap_alloc(16384, 16);
        uintptr_t sstack = (uintptr_t)kheap_alloc(16384, 16);
        gdt_entry_t *gdt = (gdt_entry_t *)kheap_alloc(sizeof(gdt_entry_t) * 7, 16);
        tss_t *tss = (tss_t *)kheap_alloc(sizeof(tss_t), 16);

        if (!kstack || !sstack || !gdt || !tss ||
            kstack + 16384 > 0xFFFFFFFFULL)
        {
            debug_printf("[boot] smp: cpu %u setup failed (OOM)\n",
                         (unsigned)i);
            continue;
        }

        ap_ctx[i].kernel_stack_top = kstack + 16384;
        ap_ctx[i].syscall_stack_top = sstack + 16384;
        ap_ctx[i].gdt = gdt;
        ap_ctx[i].tss = tss;

        gdt_build_ap(gdt, tss, ap_ctx[i].kernel_stack_top);

        volatile uint32_t *slot = (volatile uint32_t *)
            (TRAMP_BASE + TRAMP_CPU_TABLE_OFF + (uintptr_t)i * 8);
        slot[0] = (uint32_t)ap_ctx[i].kernel_stack_top;
        slot[1] = c->lapic_id;
    }

    // INIT + double SIPI per AP (Intel SDM: 10 ms gap after INIT, ~200 us
    // between the two SIPIs; a single SIPI is legal but the second one is
    // a robustness tradition)
    for (uint32_t i = 0; i < count; i++)
    {
        const smp_cpu_t *c = smp_cpu_at(i);

        if (!c->enabled || c->bootstrap)
            continue;

        lapic_send_init_ipi(c->lapic_id);
        ap_delay_ms(10);
        lapic_send_sipi(c->lapic_id, TRAMP_SIPI_VECTOR);
        ap_delay_us(200);
        lapic_send_sipi(c->lapic_id, TRAMP_SIPI_VECTOR);
    }

    // wait for every AP to reach the online counter (1 s timeout)
    uint64_t deadline = scheduler_ticks() + 100;

    while (g_ap_online < expected && scheduler_ticks() < deadline)
        pause_cpu();

    debug_printf("[boot] smp: %u/%u cpus online\n",
                 (unsigned)g_ap_online, (unsigned)expected);
}