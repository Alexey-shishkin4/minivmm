#include "tap.h"

#include "virtio/virtqueue.h"

/*
 * TODO: pop a simple chain; descriptor loop detected; chain longer than queue
 *        size rejected
 * TODO: descriptor pointing outside guest RAM rejected; avail->idx jump > queue
 *        size rejected
 */

int main(void)
{
    tap_plan(1);
    tap_ok(1, "test binary links against minivmm_core");
    return tap_done();
}
