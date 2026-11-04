/*
 * PCI host bridge and configuration space.
 *
 * TODO: config mechanism #1: CONFIG_ADDRESS 0xCF8 / CONFIG_DATA 0xCFC
 * TODO: later: ECAM (MMCONFIG) + MCFG ACPI table
 * TODO: struct pci_device: 256-byte (4 KiB with ECAM) config space,
 *        writable-bits mask, BAR sizing protocol (write all-ones)
 * TODO: BAR allocation in the MMIO hole, capability list
 * TODO: legacy INTx routing first, MSI-X later
 */

#ifndef MINIVMM_PCI_PCI_H
#define MINIVMM_PCI_PCI_H

struct pci_device;

#endif /* MINIVMM_PCI_PCI_H */
