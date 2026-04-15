// drivers/audio/intel_hda_structs.h
#ifndef INTEL_HDA_STRUCTS_H
#define INTEL_HDA_STRUCTS_H

#include <stdint.h>

#define HDA_REG_GCAP      0x00
#define HDA_REG_VMIN      0x02
#define HDA_REG_VMAJ      0x03
#define HDA_REG_OUTPAY    0x04
#define HDA_REG_INPAY     0x06
#define HDA_REG_GCTL      0x08
#define HDA_REG_WAKEEN    0x0C
#define HDA_REG_STATESTS  0x0E
#define HDA_REG_GSTS      0x10
#define HDA_REG_INTCTL    0x20
#define HDA_REG_INTSTS    0x24
#define HDA_REG_WALCLK    0x30
#define HDA_REG_SSYNC     0x38

#define HDA_REG_CORBLBASE 0x40
#define HDA_REG_CORBUBASE 0x44
#define HDA_REG_CORBWP    0x48
#define HDA_REG_CORBRP    0x4A
#define HDA_REG_CORBCTL   0x4C
#define HDA_REG_CORBSTS   0x4D
#define HDA_REG_CORBSIZE  0x4E

#define HDA_REG_RIRBLBASE 0x50
#define HDA_REG_RIRBUBASE 0x54
#define HDA_REG_RIRBWP    0x58
#define HDA_REG_RIRBRESP  0x5A
#define HDA_REG_RIRBCTL   0x5C
#define HDA_REG_RIRBSTS   0x5D
#define HDA_REG_RIRBSIZE  0x5E

#define HDA_REG_DPLBASE   0x70
#define HDA_REG_DPUBASE   0x74

#define HDA_REG_SD0_CTL   0x100
#define HDA_REG_SD0_STS   0x103
#define HDA_REG_SD0_LPIB  0x104
#define HDA_REG_SD0_CBL   0x108
#define HDA_REG_SD0_LVI   0x10C
#define HDA_REG_SD0_FMT   0x112
#define HDA_REG_SD0_BDPL  0x118
#define HDA_REG_SD0_BDPU  0x11C

#define HDA_GCTL_CRST     (1 << 0)
#define HDA_CORBCTL_RUN   (1 << 1)
#define HDA_CORBCTL_MEIE  (1 << 0)
#define HDA_RIRBCTL_RUN   (1 << 1)
#define HDA_RIRBCTL_INT   (1 << 0)

#define HDA_SD_CTL_RUN    (1 << 1)
#define HDA_SD_CTL_IOCE   (1 << 2)
#define HDA_SD_CTL_FEIE   (1 << 3)
#define HDA_SD_CTL_DEIE   (1 << 4)

#define HDA_VERB_GET_PARAM      0xF0000
#define HDA_VERB_SET_STREAM_CH  0x70600
#define HDA_VERB_SET_FMT        0x20000
#define HDA_VERB_GET_CONN_LIST  0xF0200
#define HDA_VERB_SET_PIN_CTL    0x70700
#define HDA_VERB_SET_EAPD_BTL   0x70C00
#define HDA_VERB_SET_AMP_GAIN   0x30000
#define HDA_VERB_GET_CONFIG     0xF1C00

#define HDA_PARAM_VENDOR_ID     0x00
#define HDA_PARAM_REV_ID        0x02
#define HDA_PARAM_NODE_CNT      0x04
#define HDA_PARAM_FGRP_TYPE     0x05
#define HDA_PARAM_AUDIO_WID_CAP 0x09
#define HDA_PARAM_PCM_SIZE_RATE 0x0A
#define HDA_PARAM_STREAM_FMTS   0x0B
#define HDA_PARAM_PIN_CAP       0x0C
#define HDA_PARAM_AMP_OUT_CAP   0x12
#define HDA_PARAM_CONN_LIST_LEN 0x0E

#define HDA_WIDGET_AUDIO_OUT    0x0
#define HDA_WIDGET_AUDIO_IN     0x1
#define HDA_WIDGET_AUDIO_MIX    0x2
#define HDA_WIDGET_AUDIO_SEL    0x3
#define HDA_WIDGET_PIN          0x4
#define HDA_WIDGET_POWER        0x5
#define HDA_WIDGET_VOL_KNOB     0x6

#define HDA_PIN_OUT_ENABLE      (1 << 6)
#define HDA_PIN_HP_ENABLE       (1 << 7)

typedef struct {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed)) hda_bdl_entry_t;

typedef struct {
    uint8_t channels;
    uint8_t bits;
    uint32_t rate;
} hda_pcm_format_t;

#endif
