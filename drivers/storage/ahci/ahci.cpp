// drivers/storage/ahci/ahci.cpp
#include "drivers/storage/ahci/ahci.hpp"
#include "drivers/pci/pci.hpp"
#include "memory/paging.h"
#include "memory/pmm.h"
#include "memory/kheap.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
#include "system/disk/gpt.h"
#include "kernel/debug.h"
#include "system/power/power.h"
extern "C" void print_status(const char* prefix, const char* msg, const char* status);
extern "C" uint64_t get_physical_address(uint64_t virtual_addr);
extern "C" void* kmalloc_contiguous(size_t size);
extern "C" void kfree_contiguous(void* ptr, size_t size);
extern "C" void tsc_delay_ms(uint32_t ms);

static AHCIDriver* g_ahci_driver = nullptr;

class AHCISubDevice : public Device {
private:
    AHCIPort* port;
public:
    AHCISubDevice(AHCIPort* p, const char* name, const char* model)
        : Device(name, DEV_BLOCK), port(p)
    {
        uint64_t block_size = port->is_atapi ? 2048 : 512;
        this->setCapacity(port->sector_count * block_size);
        this->setModel(model);
    }

    int init() override { return 1; }

    uint32_t getBlockSize() const override { return port->is_atapi ? 2048 : 512; }

    int readBlock(uint64_t lba, uint32_t count, void* buffer) override {
        return port->read(lba, count, buffer);
    }

    int writeBlock(uint64_t lba, uint32_t count, const void* buffer) override {
        if (this->isWriteProtected()) {
            printf("[AHCI] Blocked write to '%s' (Device Lockdown Active).\n", this->getName());
            return 0;
        }
        return port->write(lba, count, buffer);
    }
};

AHCIDriver::AHCIDriver(PCIeDevice* pci)
    : Device("AHCI Controller", DEV_BLOCK), pciDev(pci)
{
    portCount = 0;
    initialized_early = false;
}

AHCIDriver::~AHCIDriver() {
    for (int i = 0; i < portCount; i++) {
        if (ports[i]) {
            ports[i]->stopCmd();
            delete ports[i];
            ports[i] = nullptr;
        }
    }
}

int AHCIDriver::earlyInit() {
    pciDev->enableBusMaster();
    pciDev->enableMemorySpace();

    uint32_t bar5 = pciDev->getBAR(5);
    if (bar5 == 0) { return 0; }

    uint64_t phys_addr = bar5 & 0xFFFFFFF0ULL;
    void* virt_addr = ioremap(phys_addr, 8192,
                               PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_PWT);
    if (!virt_addr) { return 0; }

    abar = (volatile hba_mem_t*)virt_addr;

    abar->ghc |= (1u << 0);

    int wait = 0;
    while ((abar->ghc & (1u << 0)) && wait < 100) {
        tsc_delay_ms(10);
        wait++;
    }

    abar->ghc |= (1u << 31);
    abar->ghc |= (1u << 1);

    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1u << i)) {
            volatile hba_port_t* port =
                (volatile hba_port_t*)((uint8_t*)abar + 0x100 + (i * 0x80));
            port->cmd |= (1u << 1) | (1u << 2);
        }
    }

    initialized_early = true;
    return 1;
}

int AHCIDriver::finalize() {
    if (!initialized_early) return 0;

    tsc_delay_ms(10);

    uint32_t pi = abar->pi;

    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) continue;

        volatile hba_port_t* port = (volatile hba_port_t*)((uint8_t*)abar + 0x100 + (i * 0x80));

        uint32_t ssts = port->ssts;
        uint8_t ipm = (ssts >> 8) & 0x0F;
        uint8_t det = ssts & 0x0F;

        if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE) {
            continue;
        }

        AHCIPort* newPort = new AHCIPort();
        newPort->id      = i;
        newPort->hbaPort = port;
        newPort->hbaMem  = abar;

        if (newPort->configure() == 0) {
            ports[portCount++] = newPort;

            char newName[32];
            DeviceManager::getNextSataName(newName);

            if (newPort->is_atapi) sprintf(newName, "cdrom%d", i);

            AHCISubDevice* subDev = new AHCISubDevice(newPort, newName, newPort->model_name);
            DeviceManager::registerDevice(subDev);

            char msg[64];
            sprintf(msg, "Port %d attached as %s", i, newName);
            print_status("[ AHCI ]", msg, "INFO");

            if (!newPort->is_atapi) gpt_scan_partitions(subDev);
        } else {
            delete newPort;
        }
    }

    return (portCount > 0) ? 1 : 0;
}

int AHCIDriver::init() {
    if (earlyInit()) { return finalize(); }
    return 0;
}

int AHCIDriver::shutdown() {
    for (int i = 0; i < portCount; i++) {
        if (ports[i]) { ports[i]->stopCmd(); }
    }
    print_shutdown("AHCI DMA engines stopped");
    return 1;
}

void AHCIDriver::emergencyStop() {
    if (abar) { abar->ghc |= 1u; }
}

int AHCIDriver::readBlock(uint64_t lba, uint32_t count, void* buffer) {
    (void)lba; (void)count; (void)buffer; return 0;
}

int AHCIDriver::writeBlock(uint64_t lba, uint32_t count, const void* buffer) {
    (void)lba; (void)count; (void)buffer; return 0;
}

extern "C" {
    __attribute__((used, noinline))
    void init_ahci() {
        for (int i = 0; i < PCIe::getDeviceCount(); i++) {
            PCIeDevice* pci = PCIe::getDevice(i);
            if (pci->class_id == PCI_CLASS_STORAGE && pci->subclass_id == PCI_SUBCLASS_AHCI) {
                AHCIDriver* drv = new AHCIDriver(pci);
                drv->init();
                return;
            }
        }
    }

    __attribute__((used, noinline))
    void init_ahci_early() {
        for (int i = 0; i < PCIe::getDeviceCount(); i++) {
            PCIeDevice* pci = PCIe::getDevice(i);
            if (pci->class_id == PCI_CLASS_STORAGE && pci->subclass_id == PCI_SUBCLASS_AHCI) {
                g_ahci_driver = new AHCIDriver(pci);
                g_ahci_driver->earlyInit();
                return;
            }
        }
    }

    __attribute__((used, noinline))
    void init_ahci_late() {
        if (g_ahci_driver) { g_ahci_driver->finalize(); }
    }
}
