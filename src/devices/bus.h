/*
 * Port-I/O and MMIO dispatch: maps address ranges to device callbacks.
 *
 * TODO: struct bus + register(base, len, ops, opaque); reject overlaps
 * TODO: dispatch(addr, data, len, is_write) from vcpu exits
 * TODO: sorted array + binary search; measure before optimizing
 * TODO: locking model once there is more than one vCPU
 */

#ifndef MINIVMM_DEVICES_BUS_H
#define MINIVMM_DEVICES_BUS_H

struct bus;

#endif /* MINIVMM_DEVICES_BUS_H */
