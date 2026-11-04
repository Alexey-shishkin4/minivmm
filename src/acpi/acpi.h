/*
 * Minimal ACPI tables so the guest discovers CPUs, interrupts and PCI.
 *
 * TODO: RSDP (in EBDA/0xE0000), XSDT, FADT (hardware-reduced?), MADT
 *        (LAPIC per vCPU, IOAPIC, ISA overrides), DSDT (AML: PCI root)
 * TODO: checksums; compare with `acpidump` inside the guest
 * TODO: alternative for the first SMP step: Intel MP tables
 */

#ifndef MINIVMM_ACPI_ACPI_H
#define MINIVMM_ACPI_ACPI_H

struct vmm;

#endif /* MINIVMM_ACPI_ACPI_H */
