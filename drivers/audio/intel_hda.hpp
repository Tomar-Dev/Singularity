// drivers/audio/intel_hda.hpp
#ifndef INTEL_HDA_HPP
#define INTEL_HDA_HPP

#include "system/device/device.h"
#include "drivers/pci/pci.hpp"
#include "drivers/audio/intel_hda_structs.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
class IntelHDADriver : public Device {
private:
    PCIeDevice* pciDev;
    uint64_t base_phys;
    volatile uint8_t* regs;
    
    uint32_t* corb_virt; 
    uint64_t* rirb_virt; 
    int corb_entries;
    int rirb_entries;
    uint16_t corb_wp;
    uint16_t rirb_rp;
    
    spinlock_t cmd_lock;
    
    hda_bdl_entry_t* bdl_virt; 
    void* dma_buffer_virt;     
    uint32_t dma_buffer_size;
    
    uint8_t codec_addr;
    uint8_t afg_nid;     
    uint8_t output_nid;  
    uint8_t pin_nid;     

    void write32(uint32_t offset, uint32_t val);
    uint32_t read32(uint32_t offset);
    void write16(uint32_t offset, uint16_t val);
    uint16_t read16(uint32_t offset);
    void write8(uint32_t offset, uint8_t val);
    uint8_t read8(uint32_t offset);
    
    int resetController();
    int initCORB();
    int initRIRB();
    uint64_t sendCommand(uint8_t nid, uint32_t verb, uint32_t payload);
    
    void enumerateCodec();
    void parseWidgetGraph();
    void setupStream(uint32_t frequency, uint8_t channels);

public:
    IntelHDADriver(PCIeDevice* pci);
    ~IntelHDADriver();
    
    int init() override;
    int shutdown() override;
    
    // FIX: Takes dynamic buffer from User Space / VFS, no hardcoded arrays
    void playAudio(void* buffer, uint32_t size);
};

#ifdef __cplusplus
extern "C" {
#endif

void init_intel_hda();

#ifdef __cplusplus
}
#endif

#endif
