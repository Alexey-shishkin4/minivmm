/*
 * Split virtqueue (virtio 1.x spec, section 2.7): descriptor table, avail and used rings.
 *
 * TODO: pop available chain -> iovec array, push used element, notify policy
 *        (VIRTIO_F_EVENT_IDX)
 * TODO: THIS IS THE ATTACK SURFACE: indices, lengths, descriptor loops,
 *        indirect descriptors, chains longer than queue size -- all
 *        guest controlled. Validate everything; fuzzed in tests/fuzz.
 * TODO: memory barriers between reading avail->idx and descriptors
 */

#ifndef MINIVMM_VIRTIO_VIRTQUEUE_H
#define MINIVMM_VIRTIO_VIRTQUEUE_H

struct virtqueue;

#endif /* MINIVMM_VIRTIO_VIRTQUEUE_H */
