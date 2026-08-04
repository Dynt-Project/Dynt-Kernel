#ifndef DRIVER_STACKS_STORAGE_PARTITION_H
#define DRIVER_STACKS_STORAGE_PARTITION_H

#include "block.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t partition_scan_all(void);
uint32_t partition_scan_device(block_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif // DRIVER_STACKS_STORAGE_PARTITION_H
