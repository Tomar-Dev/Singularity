// drivers/audio/intel_hda.cpp
#include "drivers/audio/intel_hda.hpp"
#include "memory/paging.h"
#include "memory/kheap.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
extern "C" void yield(); 
extern "C" uint64_t get_physical_address(uint64_t v);

IntelHDADriver::IntelHDADriver(PCIeDevice* pci) 
    : Device("Intel HDA", DEV_UNKNOWN), pciDev(pci)
{
    spinlock_init(&cmd_lock);
    codec_addr = 0;
    afg_nid = 0;
    output_nid = 0;
    pin_nid = 0;
    
    bdl_virt = nullptr;
    dma_buffer_virt = nullptr;
    dma_buffer_size = 0;
}

IntelHDADriver::~IntelHDADriver() {}

void IntelHDADriver::write32(uint32_t offset, uint32_t val) { *(volatile uint32_t*)(regs + offset) = val; }
uint32_t IntelHDADriver::read32(uint32_t offset) { return *(volatile uint32_t*)(regs + offset); }
void IntelHDADriver::write16(uint32_t offset, uint16_t val) { *(volatile uint16_t*)(regs + offset) = val; }
uint16_t IntelHDADriver::read16(uint32_t offset) { return *(volatile uint16_t*)(regs + offset); }
void IntelHDADriver::write8(uint32_t offset, uint8_t val) { *(volatile uint8_t*)(regs + offset) = val; }
uint8_t IntelHDADriver::read8(uint32_t offset) { return *(volatile uint8_t*)(regs + offset); }

int IntelHDADriver::resetController() {
    uint32_t gctl = read32(HDA_REG_GCTL);
    write32(HDA_REG_GCTL, gctl & ~HDA_GCTL_CRST); 
    
    int timeout = 1000;
    while ((read32(HDA_REG_GCTL) & HDA_GCTL_CRST) && timeout--) {
        for(int k=0; k<100; k++) hal_cpu_relax();
    }
    if (timeout <= 0) return 0;
    
    write32(HDA_REG_GCTL, gctl | HDA_GCTL_CRST); 
    timeout = 1000;
    while (!(read32(HDA_REG_GCTL) & HDA_GCTL_CRST) && timeout--) {
        for(int k=0; k<100; k++) hal_cpu_relax();
    }
    if (timeout <= 0) return 0;
    
    for(int i=0; i<10000; i++) hal_cpu_relax();
    return 1;
}

int IntelHDADriver::initCORB() {
    write8(HDA_REG_CORBCTL, 0);
    
    uint8_t size = read8(HDA_REG_CORBSIZE);
    if ((size & 0x40) == 0x40) {
        write8(HDA_REG_CORBSIZE, 0x02); 
        corb_entries = 256;
    } else if ((size & 0x20) == 0x20) {
        write8(HDA_REG_CORBSIZE, 0x01); 
        corb_entries = 16;
    } else {
        write8(HDA_REG_CORBSIZE, 0x00); 
        corb_entries = 2;
    }
    
    corb_virt = (uint32_t*)kmalloc_aligned(corb_entries * 4, 128);
    if (!corb_virt) return 0;
    memset(corb_virt, 0, corb_entries * 4);
    
    uint64_t phys = get_physical_address((uint64_t)corb_virt);
    write32(HDA_REG_CORBLBASE, (uint32_t)phys);
    write32(HDA_REG_CORBUBASE, (uint32_t)(phys >> 32));
    
    write16(HDA_REG_CORBWP, 0);
    corb_wp = 0;
    
    write16(HDA_REG_CORBRP, 0x8000); 
    while (!(read16(HDA_REG_CORBRP) & 0x8000));
    write16(HDA_REG_CORBRP, 0);
    while (read16(HDA_REG_CORBRP) & 0x8000);
    
    write8(HDA_REG_CORBCTL, HDA_CORBCTL_RUN | HDA_CORBCTL_MEIE);
    return 1;
}

int IntelHDADriver::initRIRB() {
    write8(HDA_REG_RIRBCTL, 0);
    
    uint8_t size = read8(HDA_REG_RIRBSIZE);
    if ((size & 0x40) == 0x40) {
        write8(HDA_REG_RIRBSIZE, 0x02); 
        rirb_entries = 256;
    } else {
        rirb_entries = 256; 
    }
    
    rirb_virt = (uint64_t*)kmalloc_aligned(rirb_entries * 8, 128);
    if (!rirb_virt) return 0;
    memset(rirb_virt, 0, rirb_entries * 8);
    
    uint64_t phys = get_physical_address((uint64_t)rirb_virt);
    write32(HDA_REG_RIRBLBASE, (uint32_t)phys);
    write32(HDA_REG_RIRBUBASE, (uint32_t)(phys >> 32));
    
    write16(HDA_REG_RIRBWP, 0x8000); 
    rirb_rp = 0;
    
    write8(HDA_REG_RIRBCTL, HDA_RIRBCTL_RUN | HDA_RIRBCTL_INT);
    return 1;
}

uint64_t IntelHDADriver::sendCommand(uint8_t nid, uint32_t verb, uint32_t payload) {
    uint64_t flags = spinlock_acquire(&cmd_lock);
    
    uint32_t val = (codec_addr << 28) | (nid << 20) | verb | payload;
    
    corb_wp++;
    if (corb_wp >= corb_entries) corb_wp = 0;
    
    corb_virt[corb_wp % corb_entries] = val;
    write16(HDA_REG_CORBWP, corb_wp);
    
    int timeout = 2000; 
    uint64_t response = 0;
    
    while (timeout--) {
        uint16_t wp = read16(HDA_REG_RIRBWP) & 0xFF;
        if (wp != rirb_rp) {
            rirb_rp++;
            if (rirb_rp >= rirb_entries) rirb_rp = 0;
            response = rirb_virt[rirb_rp];
            break;
        }
        hal_cpu_relax();
    }
    
    spinlock_release(&cmd_lock, flags);
    return response;
}

void IntelHDADriver::enumerateCodec() {
    uint16_t statest = read16(HDA_REG_STATESTS);
    
    for (int i = 0; i < 15; i++) {
        if ((statest >> i) & 1) {
            codec_addr = i;
            break;
        }
    }

    uint64_t res = sendCommand(0, HDA_VERB_GET_PARAM, HDA_PARAM_NODE_CNT);
    uint8_t start_node = (res >> 16) & 0xFF;
    uint8_t total_nodes = res & 0xFF;

    for (int i = 0; i < total_nodes; i++) {
        uint8_t nid = start_node + i;
        res = sendCommand(nid, HDA_VERB_GET_PARAM, HDA_PARAM_FGRP_TYPE);
        if ((res & 0xFF) == 0x01) { 
            afg_nid = nid;
            break;
        }
    }
}

void IntelHDADriver::parseWidgetGraph() {
    if (afg_nid == 0) return;

    sendCommand(afg_nid, 0x70500, 0); 

    uint64_t res = sendCommand(afg_nid, HDA_VERB_GET_PARAM, HDA_PARAM_NODE_CNT);
    uint8_t start_node = (res >> 16) & 0xFF;
    uint8_t total_nodes = res & 0xFF;

    for (int i = 0; i < total_nodes; i++) {
        uint8_t nid = start_node + i;
        uint32_t caps = sendCommand(nid, HDA_VERB_GET_PARAM, HDA_PARAM_AUDIO_WID_CAP);
        uint8_t type = (caps >> 20) & 0xF;

        if (type == HDA_WIDGET_PIN) {
            uint32_t pin_cap = sendCommand(nid, HDA_VERB_GET_PARAM, HDA_PARAM_PIN_CAP);
            if (pin_cap & (1 << 4)) { 
                uint32_t config = sendCommand(nid, HDA_VERB_GET_CONFIG, 0); 
                uint8_t device = (config >> 20) & 0xF;
                
                if (device == 0 || device == 1 || device == 2) { 
                    pin_nid = nid;
                    break; 
                }
            }
        }
    }

    if (pin_nid == 0) return;

    uint32_t conn_len = sendCommand(pin_nid, HDA_VERB_GET_PARAM, HDA_PARAM_CONN_LIST_LEN);
    uint8_t num_conn = conn_len & 0x7F;
    
    if (num_conn > 0) {
        uint32_t conn_list = sendCommand(pin_nid, HDA_VERB_GET_CONN_LIST, 0);
        uint8_t connected_nid = conn_list & 0xFF; 
        
        uint32_t caps = sendCommand(connected_nid, HDA_VERB_GET_PARAM, HDA_PARAM_AUDIO_WID_CAP);
        uint8_t type = (caps >> 20) & 0xF;
        
        if (type == HDA_WIDGET_AUDIO_OUT) {
            output_nid = connected_nid;
        } else if (type == HDA_WIDGET_AUDIO_MIX) {
             for (int i = 0; i < total_nodes; i++) {
                uint8_t nid = start_node + i;
                uint32_t c = sendCommand(nid, HDA_VERB_GET_PARAM, HDA_PARAM_AUDIO_WID_CAP);
                if (((c >> 20) & 0xF) == HDA_WIDGET_AUDIO_OUT) {
                    output_nid = nid;
                    break;
                }
             }
        }
    }
    
    if (output_nid != 0) {
        sendCommand(pin_nid, HDA_VERB_SET_PIN_CTL, HDA_PIN_OUT_ENABLE | HDA_PIN_HP_ENABLE);
        sendCommand(pin_nid, HDA_VERB_SET_AMP_GAIN, 0xB07F); 
        
        sendCommand(output_nid, HDA_VERB_SET_STREAM_CH, 0x10); 
        sendCommand(output_nid, HDA_VERB_SET_FMT, 0x0011); 
        sendCommand(output_nid, HDA_VERB_SET_AMP_GAIN, 0xB07F); 
    }
}

void IntelHDADriver::setupStream(uint32_t frequency, uint8_t channels) {
    (void)frequency; (void)channels;
    
    bdl_virt = (hda_bdl_entry_t*)kmalloc_aligned(4096, 128);
    if(bdl_virt) memset(bdl_virt, 0, 4096);
    
    write32(HDA_REG_SD0_CTL, 0); 
    while(read32(HDA_REG_SD0_CTL) & 1); 
    write32(HDA_REG_SD0_CTL, 1); 
    while(!(read32(HDA_REG_SD0_CTL) & 1));
    
    write16(HDA_REG_SD0_FMT, 0x0011); 
}

void IntelHDADriver::playAudio(void* buffer, uint32_t size) {
    if (!buffer || size == 0 || !bdl_virt) return;
    if (size > 1048576) size = 1048576; // FIX 23: HDA 1MB Bound

    write32(HDA_REG_SD0_CTL, 0);
    
    // FIX: Scatter-Gather DMA uyumsuzlugu ve RAM tasmasi onlendi.
    // Eger veri sayfa sinirlarini tasiyorsa parcalanarak (Chunking) birden fazla BDL entry olusturulur.
    uint32_t bdl_idx = 0;
    uint64_t current_virt = (uint64_t)buffer;
    uint32_t bytes_left = size;

    while (bytes_left > 0 && bdl_idx < 256) {
        uint64_t phys = get_physical_address(current_virt);
        uint32_t page_offset = current_virt & 0xFFF;
        uint32_t chunk = 4096 - page_offset;
        
        if (chunk > bytes_left) chunk = bytes_left;

        bdl_virt[bdl_idx].address = phys;
        bdl_virt[bdl_idx].length = chunk;
        bdl_virt[bdl_idx].flags = 0;

        bytes_left -= chunk;
        current_virt += chunk;
        bdl_idx++;
    }
    
    if (bdl_idx > 0) {
        bdl_virt[bdl_idx - 1].flags = 1; // IOC bit
    }
    
    uint64_t bdl_phys = get_physical_address((uint64_t)bdl_virt);
    
    write32(HDA_REG_SD0_CBL, size - bytes_left); // Gercekte haritalanabilen miktar
    write16(HDA_REG_SD0_LVI, bdl_idx > 0 ? (bdl_idx - 1) : 0); 
    write32(HDA_REG_SD0_BDPL, (uint32_t)bdl_phys);
    write32(HDA_REG_SD0_BDPU, (uint32_t)(bdl_phys >> 32));
    
    uint32_t ctl = read32(HDA_REG_SD0_CTL);
    ctl &= ~0xFF0000;
    ctl |= (1 << 20); 
    write32(HDA_REG_SD0_CTL, ctl | HDA_SD_CTL_RUN | HDA_SD_CTL_IOCE);
}

int IntelHDADriver::init() {
    pciDev->enableBusMaster();
    pciDev->enableMemorySpace();
    
    uint32_t bar0 = pciDev->getBAR(0);
    uint64_t bar0_phys = bar0 & 0xFFFFFFF0;
    if ((bar0 & 0x4) == 0x4) {
        uint32_t bar1 = pciDev->getBAR(1);
        bar0_phys |= ((uint64_t)bar1 << 32);
    }
    
    base_phys = bar0_phys;
    regs = (volatile uint8_t*)ioremap(base_phys, 16384, PAGE_PRESENT | PAGE_WRITE | PAGE_PCD);
    if (!regs) return 0;
    
    if (!resetController()) return 0;
    if (!initCORB() || !initRIRB()) return 0;
    
    enumerateCodec();
    parseWidgetGraph();
    setupStream(48000, 2);
    
    DeviceManager::registerDevice(this);
    
    return 1;
}

int IntelHDADriver::shutdown() {
    write32(HDA_REG_SD0_CTL, 0); 
    write8(HDA_REG_CORBCTL, 0);
    write8(HDA_REG_RIRBCTL, 0);
    return 1;
}

extern "C" {
    __attribute__((used, noinline))
    void init_intel_hda() {
        for (int i = 0; i < PCIe::getDeviceCount(); i++) {
            PCIeDevice* pci = PCIe::getDevice(i);
            if (pci->class_id == 0x04 && pci->subclass_id == 0x03) {
                IntelHDADriver* drv = new IntelHDADriver(pci);
                drv->init();
                return; 
            }
        }
    }
}
