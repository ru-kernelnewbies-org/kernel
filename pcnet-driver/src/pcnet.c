/* pcnet.c: PCNet-PCI II/III Ethernet controller driver */
/*
 * Authors:
 *		Dmitry Podgorny <pasis.ua@gmail.com>
 *		Denis Kirjanov <kirjanov@gmail.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "pcnet.h"

#define DRV_NAME	"pcnet_dummy"
#define DRV_VERSION	"dev"
#define DRV_DESCRIPTION	"PCNet-PCI II/III Ethernet controller driver"

MODULE_AUTHOR("Dmitry Podgorny <pasis.ua@gmail.com>");
MODULE_AUTHOR("Denis Kirjanov <kirjanov@gmail.com>");
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");

static DEFINE_PCI_DEVICE_TABLE(pcnet_dummy_pci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_LANCE) },
	{ }
};

/* PCnet-PCI II controller initialization includes the reading
 * of the initialization block in memory to obtain the operat-
 * ing parameters. 
 * When SSIZE32 (BCR20, bit 8) is set to ONE, all
 * initialization block entries are logically 32-bits wide.
 *
 * The PCnet-PCI II controller obtains the start address of
 * the initialization block from the contents of CSR1 (least
 * significant 16 bits of address) and CSR2 (most signifi-
 * cant 16 bits of address). The host must write CSR1 and
 * CSR2 before setting the INIT bit. The initialization block
 * contains the user defined conditions for PCnet-PCI II
 * controller operation, together with the base addresses
 * and length information of the transmit and receive
 * descriptor rings.
 */

/* Am79C970A reference, p. 69 */
struct pcnet_dummy_init_block {
	__le16 mode;
	/* num of entries in TX/RX rings */
	__le16 txlen_rxlen;
	/* MAC address */
	u8 mac_addr[6];
	__le16 reserved;
	/* The Logical Address Filter (LADRF) is a programmable
	 * 64-bit mask that is used to accept incoming packets
	 * based on Logical (Multicast) Addresses.
	 */
	__le32 laddr_filter_low;
	__le32 laddr_filter_hi;
	/* programmed with the physical address of the receive
	 * and transmit descriptor rings
	 */
	__le32 rx_ring;
	__le32 tx_ring;
} __attribute__ ((__packed__));

/* 32bit TX/RX descriptors */
/* PCnet Software Design Considerations, p.5 */
struct xmit_descr {
	__le32 addr;
	/* The BCNT fields of the transmit and receive descriptors
	 * are 12-bit negative numbers representing the twos com-
	 * plement of the buffer size in bytes
	 */
	__le16 size;
	__le16 status;
	__le32 flags;
	__le32 reserved;
};

struct pcnet_private {
	/* TODO:
	 * DMA buffer management operations
	 */
	spinlock_t lock;
	struct pci_dev *pci_dev;
	void __iomem *base;
};

/* 16 most significant bits of all registers are undefined on reading and 
 * must be set to 0 on writing (except of CSR88)
 */

#define read_csr(csr) pcnet_dummy_read_csr(pp->base, csr)
#define read_bcr(bcr) pcnet_dummy_read_bcr(pp->base, bcr)
#define write_csr(csr, val) pcnet_dummy_write_csr(pp->base, csr, val)
#define write_bcr(bcr, val) pcnet_dummy_write_bcr(pp->base, bcr, val)

static inline u32 pcnet_dummy_read_csr(void __iomem *ioaddr, u32 csr)
{
	iowrite32(csr, ioaddr + PCNET_RAP);
	return ioread32(ioaddr + PCNET_RDP) & 0xffff;
}

static inline u32 pcnet_dummy_read_bcr(void __iomem *ioaddr, u32 bcr)
{
	iowrite32(bcr, ioaddr + PCNET_RAP);
	return ioread32(ioaddr + PCNET_BDP) & 0xffff;
}

static inline void pcnet_dummy_write_csr(void __iomem *ioaddr, u32 csr, u32 val)
{
	iowrite32(csr, ioaddr + PCNET_RAP);
	iowrite32(val & 0xffff, ioaddr + PCNET_RDP);
}

static inline void pcnet_dummy_write_bcr(void __iomem *ioaddr, u32 bcr, u32 val)
{
	iowrite32(bcr, ioaddr + PCNET_RAP);
	iowrite32(val & 0xffff, ioaddr + PCNET_BDP);
}

static int pcnet_dummy_reset(void __iomem *ioaddr)
{
	ioread16(ioaddr + PCNET_RESET16);
	/* helper functions for WORD I/O mode ain't implemented */
	iowrite16(CSR0, ioaddr + PCNET_RAP16);
	if (ioread16(ioaddr + PCNET_RDP16) == CSR0_STOP)
		return 0;
	/* try to reset for DWORD I/O mode */
	ioread32(ioaddr + PCNET_RESET);
	if (pcnet_dummy_read_csr(ioaddr, CSR0) != CSR0_STOP)
		return -EBUSY;

	return 0;
}

static int pcnet_dummy_switch_dword_mode(void __iomem *ioaddr)
{
	/* a dword write to RDP sets controller into 32-bit I/O mode */
	iowrite32(0, ioaddr + PCNET_RDP);
	return !(pcnet_dummy_read_bcr(ioaddr, BCR18) & BCR18_DWIO);
}

static int pcnet_dummy_open(struct net_device *ndev)
{
	struct pcnet_private *pp;

	pp = netdev_priv(ndev);
	if (pcnet_dummy_reset(pp->base)) {
		netdev_err(ndev, "reset network controller failed\n");
		return -EBUSY;
	}
	if (pcnet_dummy_switch_dword_mode(pp->base)) {
		netdev_err(ndev, "switch to dword mode failed\n");
		netdev_err(ndev, "driver operates only in dword mode\n");
		return -EBUSY;
	}

	/* init DMA rings */

	return 0;
}

static int pcnet_dummy_stop(struct net_device *ndev)
{
	return 0;
}

static int pcnet_dummy_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	return 0;
}

/* net_device_ops structure is new for 2.6.31 */
static const struct net_device_ops pcnet_net_device_ops = {
	.ndo_open = pcnet_dummy_open,
	.ndo_stop = pcnet_dummy_stop,
	.ndo_start_xmit = pcnet_dummy_start_xmit
};

static int __devinit pcnet_dummy_init_netdev(struct pci_dev *pdev,
		unsigned long ioaddr)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	struct pcnet_private *pp;
	int irq;
	unsigned long i;

	if (!ndev)
		return -ENODEV;

	irq = pdev->irq;
	pp = netdev_priv(ndev);
	ndev->base_addr = ioaddr;
	ndev->irq = irq;
	pp->pci_dev = pdev;
	pp->base = (void *)ioaddr;
	spin_lock_init(&pp->lock);

	/* read first 6 bytes of PROM */
	for(i = 0; i < 6; i++)
		ndev->dev_addr[i] = ioread8((void *)ioaddr + i);
	if (!is_valid_ether_addr(ndev->dev_addr))
		random_ether_addr(ndev->dev_addr);

	if (pcnet_dummy_reset((void *)ioaddr))
		return -ENODEV;

	/* init net_dev_ops */
	ndev->netdev_ops = &pcnet_net_device_ops;

	if (register_netdev(ndev))
		return -ENODEV;
	netdev_info(ndev, "%s %pM\n", DRV_DESCRIPTION, ndev->dev_addr);

	return 0;
}

static int __devinit pcnet_dummy_init_one(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	struct pcnet_private *pp;
	struct net_device *ndev;
	void __iomem *ioaddr;
#ifdef USE_IO_OPS
	int bar = 0;
#else
	int bar = 1;
#endif

#ifndef MODULE
	pr_info_once("%s version %s\n", DRV_DESCRIPTION, DRV_VERSION);
#endif

	if (pci_enable_device(pdev))
		return -ENODEV;
	/* enables bus-mastering for device pdev */
	pci_set_master(pdev);

	ndev = alloc_etherdev(sizeof(*pp));
	if (!ndev)
		goto out;
	/* register ourself under /sys/class/net/ */
	SET_NETDEV_DEV(ndev, &pdev->dev);

	if (pci_request_regions(pdev, DRV_NAME))
		goto out_netdev;

	ioaddr = pci_iomap(pdev, bar, PCNET_IOSIZE_LEN);
	if (!ioaddr)
		goto out_res;
	pci_set_drvdata(pdev, ndev);

	if (pcnet_dummy_init_netdev(pdev, (unsigned long)ioaddr))
		goto out_res_unmap;

	return 0;

out_res_unmap:
	pci_iounmap(pdev, ioaddr);
out_res:
	pci_release_regions(pdev);
out_netdev:
	free_netdev(ndev);
out:
	pci_disable_device(pdev);
	return -ENODEV;
}

static void __devexit pcnet_dummy_remove_one(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	struct pcnet_private *pp;

	pp = netdev_priv(ndev);
	pcnet_dummy_reset(pp->base);
	unregister_netdev(ndev);
	pci_iounmap(pdev, pp->base);
	free_netdev(ndev);
	pci_disable_device(pdev);
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
}

static struct pci_driver pcnet_dummy_driver = {
	.name		= DRV_NAME,
	.id_table	= pcnet_dummy_pci_tbl,
	.probe		= pcnet_dummy_init_one,
	.remove		= __devexit_p(pcnet_dummy_remove_one),
#if 0
	/* FIXME: add PM hooks */
#endif
};

static int __init pcnet_init(void)
{
#ifdef MODULE
	pr_info("%s version %s\n", DRV_DESCRIPTION, DRV_VERSION);
#endif

	return pci_register_driver(&pcnet_dummy_driver);
}

static void __exit pcnet_exit(void)
{
	pci_unregister_driver(&pcnet_dummy_driver);
}

module_init(pcnet_init);
module_exit(pcnet_exit);
