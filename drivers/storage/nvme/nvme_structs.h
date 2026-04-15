// drivers/storage/nvme/nvme_structs.h
#ifndef NVME_STRUCTS_H
#define NVME_STRUCTS_H

#include <stdint.h>

typedef struct {
    uint64_t cap;
    uint32_t vs;
    uint32_t intms;
    uint32_t intmc;
    uint32_t cc;
    uint32_t rsv0;   
    uint32_t csts;
    uint32_t nssr;
    uint32_t aqa;
    uint64_t asq;
    uint64_t acq;
    uint32_t cmbloc;
    uint32_t cmbsz;
    uint32_t bpinfo;
    uint32_t bprsel;
    uint64_t bpmbl;
    uint64_t cmbmsc;
    uint32_t cmbsts;
    uint8_t  rsv1[3488]; // Reserved up to 0x1000
    uint32_t doorbells[1024]; // Doorbells (Start at 0x1000)
} __attribute__((packed)) nvme_registers_t;

#define NVME_CC_EN  (1 << 0)
#define NVME_CC_CSS_NVM (0 << 4)
#define NVME_CC_MPS_4KB (0 << 7)
#define NVME_CC_IOSQES_64 (6 << 16)
#define NVME_CC_IOCQES_16 (4 << 20)

#define NVME_CSTS_RDY (1 << 0)

typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t rsv0;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sq_entry_t;

typedef struct {
    uint32_t cdw0;
    uint32_t rsv0;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed)) nvme_cq_entry_t;

#define NVME_OP_ADMIN_CREATE_SQ 0x01
#define NVME_OP_ADMIN_CREATE_CQ 0x05
#define NVME_OP_ADMIN_IDENTIFY  0x06

#define NVME_OP_WRITE 0x01
#define NVME_OP_READ  0x02

#endif
